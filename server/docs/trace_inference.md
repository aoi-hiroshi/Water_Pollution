# Pollution trace classification

## Model lineage

The implementation follows `分类任务/classification.ipynb` rather than the
placeholder values previously shown by Qt.

- Best recorded model: Random Forest.
- Training library: scikit-learn 1.6.1.
- Model input: ten water-quality features.
- Preprocessing: the fitted `StandardScaler` saved by the notebook.
- Model classes: `0..5`.
- Business labels: company `1..6` through `label_encoder.pkl`.

The notebook performs sliding Z-score outlier detection and simplified
previous-value replacement before training (its "Kalman" update degenerates
to forward hold, not a complete Kalman filter). That cleaning belongs to the data
ingestion pipeline, not to a single-row HTTP inference request. Consequently,
raw-table inference needs cleaned, inference-ready values. Alternatively use
the new append-only cleaning API and pass `cleaning_run_id` to classification.
See [classification preprocessing](classification_preprocessing.md); per-company
isolation and the new missing-value policy differ from the original full CSV
cleaning and need task-level accuracy evaluation.
Sending an uncleaned raw outlier directly to the classifier breaks the
training-serving contract even though the tensor shape is valid.

The export script combines the scaler and classifier into one ONNX graph. This
prevents the C++ server from accidentally using a different mean, variance, or
feature order.

## Feature order

| Position | Database field | Training column |
| --- | --- | --- |
| 1 | `temperature` | `水温(℃)` |
| 2 | `ph` | `pH值(无量纲)` |
| 3 | `cod` | `化学需氧量(mg/l)` |
| 4 | `nh3n` | `氨氮(mg/l)` |
| 5 | `tp` | `总磷(mg/l)` |
| 6 | `water_level` | `液位(无)` |
| 7 | `orp` | `ORP(无)` |
| 8 | `conductivity` | `电导率(μs/cm)` |
| 9 | `dissolved_oxygen` | `溶解氧(mg/l)` |
| 10 | `turbidity` | `浊度(无)` |

## Export model

Use Python 3.10–3.12. The currently saved pickle files were produced with
scikit-learn 1.6.1, so the exporter pins that version.

```bash
python3 -m venv .venv-model
source .venv-model/bin/activate
python -m pip install -r tools/requirements-model-export.txt
python tools/export_trace_model.py
```

The exporter checks the saved class labels and feature counts, creates the
ONNX graph and compares 128 spread-out float32 inputs with sklearn outputs.
The verified ONNX/manifest/golden files are included; Linux only needs the
C++ ONNX Runtime package, not Python or pickle files. Labels matched, maximum
probability error was 6.556510925292969e-7. The exporter explicitly retains
float64 scaler constants and float32 rounding after subtraction and division,
to preserve tree routing near thresholds.

## Server build

Install the ONNX Runtime C/C++ package, then configure the existing build with:

```bash
cmake -S server -B build/server \
  -DCMAKE_BUILD_TYPE=Release \
  -DWATER_WITH_MYSQL=ON \
  -DWATER_WITH_MUDUO=ON \
  -DWATER_WITH_ONNX=ON \
  -DWATER_BUILD_TESTS=ON \
  -DWATER_ONNXRUNTIME_INCLUDE_DIR=/path/to/onnxruntime/include \
  -DWATER_ONNXRUNTIME_LIBRARY=/path/to/onnxruntime/lib/libonnxruntime.so

cmake --build build/server -j
```

Configure the runtime before starting the server:

```bash
export WATER_TRACE_MODEL_PATH="$PWD/server/models/trace_random_forest.onnx"
export WATER_TRACE_MODEL_NAME=random_forest
export WATER_TRACE_MODEL_VERSION=1.0
export WATER_TRACE_LABEL_OFFSET=1
export WATER_INFERENCE_CORE_THREADS=1
export WATER_INFERENCE_MAX_THREADS=2
export WATER_INFERENCE_QUEUE_CAPACITY=128
export WATER_ONNX_INTRA_OP_THREADS=1
```

The application thread pool controls concurrent inference. ONNX Runtime uses
one intra-op thread per request by default, avoiding nested thread-pool
oversubscription.

## Business flow

```text
Qt sends dataset + sample_id
  -> TraceController validates JSON
  -> database pool loads the ten features
  -> inference pool executes ONNX Runtime
  -> class probabilities are mapped to company_info by company_id
  -> candidates are sorted by probability
  -> Qt renders the winning company and complete ranking
```

`company_info.company_id` is treated as the business label. If a model label
has no matching company record, the response falls back to `公司N` so an
incomplete display mapping does not corrupt the probability result.

## API

```http
POST /api/v1/inference/classify
Content-Type: application/json

{
  "sample_id": 1,
  "dataset": "test_data",
  "cleaning_run_id": 100
}
```

100 is an example saved-version ID. Omit it or use 0 to read raw-table values.
`sample_id` remains the source sample ID, not the version ID.

Success response:

```json
{
  "code": 200,
  "message": "trace classification completed",
  "data": {
    "sample_id": 1,
    "dataset": "test_data",
    "model": {"name": "random_forest", "version": "1.0"},
    "predicted": {
      "rank": 1,
      "class_label": 1,
      "company_id": 1,
      "company_name": "公司1",
      "company_code": "COMPANY-001",
      "probability": 0.93
    },
    "candidates": []
  }
}
```

The endpoint returns 503 `INFERENCE_UNAVAILABLE` when the server was built
without ONNX Runtime or no model path was configured. Other data APIs remain
available in that state.
