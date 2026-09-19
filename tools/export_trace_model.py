"""Export the trained trace Random Forest pipeline to ONNX.

The notebook trained the classifier with standardized inputs. This exporter
combines the saved StandardScaler and RandomForestClassifier into one ONNX
graph so the C++ server accepts the ten raw water-quality values directly.
"""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path

import joblib
import numpy as np
import onnx
import onnxruntime as ort
from sklearn import __version__ as sklearn_version
from sklearn.pipeline import Pipeline
from skl2onnx import convert_sklearn
from skl2onnx.common.data_types import FloatTensorType


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = PROJECT_ROOT / "分类任务" / "data"
OUTPUT_DIR = PROJECT_ROOT / "server" / "models"
MODEL_PATH = OUTPUT_DIR / "trace_random_forest.onnx"
MANIFEST_PATH = OUTPUT_DIR / "trace_random_forest.json"
GOLDEN_PATH = OUTPUT_DIR / "trace_random_forest.golden.json"

TRAINING_FEATURES = [
    "水温(℃)",
    "pH值(无量纲)",
    "化学需氧量(mg/l)",
    "氨氮(mg/l)",
    "总磷(mg/l)",
    "液位(无)",
    "ORP(无)",
    "电导率(μs/cm)",
    "溶解氧(mg/l)",
    "浊度(无)",
]

API_FEATURES = [
    "temperature",
    "ph",
    "cod",
    "nh3n",
    "tp",
    "water_level",
    "orp",
    "conductivity",
    "dissolved_oxygen",
    "turbidity",
]


def require_equal(actual: object, expected: object, description: str) -> None:
    if actual != expected:
        raise RuntimeError(
            f"{description} mismatch: expected {expected!r}, got {actual!r}"
        )


def load_validation_rows(limit: int = 128) -> np.ndarray:
    rows: list[list[float]] = []
    with (SOURCE_DIR / "test_cleaned.csv").open(
        "r", encoding="utf-8-sig", newline=""
    ) as stream:
        reader = csv.DictReader(stream)
        for row in reader:
            rows.append([float(row[name]) for name in TRAINING_FEATURES])
    if not rows:
        raise RuntimeError("test_cleaned.csv contains no validation rows")
    indices = np.linspace(0, len(rows) - 1, min(limit, len(rows)), dtype=int)
    return np.asarray(rows, dtype=np.float32)[indices]


def main() -> None:
    scaler = joblib.load(SOURCE_DIR / "logistic_regression_scaler.pkl")
    classifier = joblib.load(SOURCE_DIR / "random_forest_model.pkl")
    label_encoder = joblib.load(SOURCE_DIR / "label_encoder.pkl")

    require_equal(int(scaler.n_features_in_), 10, "scaler feature count")
    require_equal(scaler.feature_names_in_.tolist(), TRAINING_FEATURES, "scaler feature order")
    require_equal(bool(scaler.with_mean), True, "scaler centering")
    require_equal(bool(scaler.with_std), True, "scaler scaling")
    require_equal(int(classifier.n_features_in_), 10, "model feature count")
    require_equal(
        np.asarray(classifier.classes_).tolist(),
        [0, 1, 2, 3, 4, 5],
        "Random Forest classes",
    )
    require_equal(
        np.asarray(label_encoder.classes_).tolist(),
        [1, 2, 3, 4, 5, 6],
        "business labels",
    )

    pipeline = Pipeline(
        [("scaler", scaler), ("classifier", classifier)]
    )
    onnx_model = convert_sklearn(
        classifier,
        name="water_trace_random_forest",
        initial_types=[("scaled_features", FloatTensorType([None, 10]))],
        target_opset=17,
        options={id(classifier): {"zipmap": False}},
    )

    # sklearn's float32 StandardScaler transforms in-place: each subtraction
    # and division uses fitted float64 constants, then rounds to float32.
    # The default ONNX Scaler rounds the constants and multiplies by 1/scale,
    # which can change tree routing near a threshold. Encode the two rounding
    # steps explicitly; do not hide routing differences by relaxing tolerance.
    onnx_model.graph.input[0].name = "features"
    onnx_model.graph.initializer.extend([
        onnx.numpy_helper.from_array(np.asarray(scaler.mean_, dtype=np.float64), "trace_mean"),
        onnx.numpy_helper.from_array(np.asarray(scaler.scale_, dtype=np.float64), "trace_scale"),
    ])
    scaler_nodes = [
        onnx.helper.make_node("Cast", ["features"], ["raw_double"], to=onnx.TensorProto.DOUBLE),
        onnx.helper.make_node("Sub", ["raw_double", "trace_mean"], ["centered_double"]),
        onnx.helper.make_node("Cast", ["centered_double"], ["centered_float"], to=onnx.TensorProto.FLOAT),
        onnx.helper.make_node("Cast", ["centered_float"], ["rounded_double"], to=onnx.TensorProto.DOUBLE),
        onnx.helper.make_node("Div", ["rounded_double", "trace_scale"], ["scaled_double"]),
        onnx.helper.make_node("Cast", ["scaled_double"], ["scaled_features"], to=onnx.TensorProto.FLOAT),
    ]
    tree_nodes = list(onnx_model.graph.node)
    del onnx_model.graph.node[:]
    onnx_model.graph.node.extend(scaler_nodes + tree_nodes)

    onnx.checker.check_model(onnx_model)
    model_bytes = onnx_model.SerializeToString()

    manifest = {
        "model_name": "random_forest",
        "model_version": "1.0",
        "source": "分类任务/classification.ipynb",
        "sklearn_version": sklearn_version,
        "sha256": hashlib.sha256(model_bytes).hexdigest(),
        "input_name": "features",
        "input_shape": [None, 10],
        "training_features": TRAINING_FEATURES,
        "api_features": API_FEATURES,
        "model_classes": [0, 1, 2, 3, 4, 5],
        "business_labels": [1, 2, 3, 4, 5, 6],
        "label_offset": 1,
        "preprocessing": "StandardScaler embedded in ONNX graph",
        "precision_contract": "float32 physical inputs; fitted float64 scaler constants; round after Sub and Div",
    }

    validation_rows = load_validation_rows()
    expected_labels = pipeline.predict(validation_rows)
    expected_probabilities = pipeline.predict_proba(validation_rows)

    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(model_bytes, sess_options=options,
                                   providers=["CPUExecutionProvider"])
    actual_labels, actual_probabilities = session.run(None, {"features": validation_rows})
    np.testing.assert_array_equal(actual_labels, expected_labels)
    np.testing.assert_allclose(actual_probabilities, expected_probabilities, rtol=1e-5, atol=1e-6)
    maximum_error = float(np.max(np.abs(actual_probabilities - expected_probabilities)))
    manifest["validation"] = {"sample_count": len(validation_rows), "max_probability_error": maximum_error,
                               "rtol": 1e-5, "atol": 1e-6}
    golden = {"input": validation_rows.tolist(), "labels": (expected_labels + 1).tolist(),
              "probabilities": expected_probabilities.tolist(), "label_offset": 1,
              "rtol": 1e-5, "atol": 1e-6, "sha256": manifest["sha256"]}
    # Only publish artifacts after both graph validation and parity succeed.
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    MODEL_PATH.write_bytes(model_bytes)
    MANIFEST_PATH.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    GOLDEN_PATH.write_text(json.dumps(golden, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Parity check passed for {len(validation_rows)} samples; max error {maximum_error}")

    print(f"ONNX model: {MODEL_PATH}")
    print(f"Manifest  : {MANIFEST_PATH}")


if __name__ == "__main__":
    main()
