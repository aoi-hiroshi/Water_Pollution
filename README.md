# Water Quality Analysis System

The project is being migrated from a Flask prototype to a C++20 Muduo server.
The desktop client remains Qt and communicates exclusively through HTTP/JSON.

```text
Qt UI
  -> QNetworkAccessManager
  -> Muduo HTTP codec and router
  -> Controller
  -> Service
  +-> Repository -> DbExecutor -> MySQL connection pool
  +-> TraceService -> inference thread pool -> ONNX Runtime
  +-> ForecastService -> inference thread pool -> ONNX Runtime
  +-> ClassificationDataService -> analysis thread pool -> optional version save
```

## Current implementation

- Qt 5 client with GET/POST JSON support, timeouts and uniform errors.
- Incremental HTTP/1.1 codec and asynchronous Muduo adapter.
- Versioned router and API controller.
- Company-list and water-data-overview services.
- Classification median missing-value repair, sliding Z-score/forward-hold cleaning,
  distributions and Pearson/Spearman correlations, with Qt charts and PNG export.
- Append-only transactional cleaning versions; classification can read a selected version.
- Database-sample trace classification, ONNX Runtime adapter and Qt ranking UI.
- Model exporter that embeds the fitted scaler and Random Forest in one ONNX graph.
- 120-row history -> 10-step / four-target Attention-LSTM forecast service,
  physical-unit ONNX adapter, Qt curves/table/CSV and an offline model exporter.
- C++20 adaptive thread pool.
- MySQL connection abstraction, prepared-statement adapter and connection pool.
- Database executor that keeps blocking MySQL work away from IO loops.
- Unit tests for the HTTP core, thread pool, connection pool and business services.

The old Flask and PyMySQL entry points have been removed. Python classification
and prediction scripts remain offline training assets. The classification
artifacts can be exported with `tools/export_trace_model.py`; runtime inference
is performed by the C++ service rather than Python.

Prediction uses `best_model_four_head_h10_diff.pth`, `scaler_y.pkl` and
`scaler_aux.pkl`, exported together by `tools/export_forecast_model.py`.
Do not mix the older checkpoints or feature-engineering metadata with this bundle.
The prediction ONNX binary and golden input/output fixture are included.
Its outputs were checked against the original notebook and saved predictions,
and the C++ ONNX Runtime adapter was compiled and exercised with the real model.
Qt compilation and 63 component/API/real-model/Qt-smoke check groups passed on Windows.
The classification ONNX binary and golden fixture are now included; 128 sampled
float32 inputs have matching sklearn labels and probabilities (max error 6.56e-7).
Cleaning/analysis component and HTTP pipeline tests and the updated Qt build passed.
The real C++ classification adapter also passed its 128-row golden test, and
the Qt mock-HTTP test passed version saving, classification handoff and six chart variants.
Linux/Muduo/MySQL
end-to-end checks require the appropriate dependencies and a prepared database.

See [Muduo server documentation](server/docs/muduo_server.md),
[thread pool documentation](server/docs/thread_pool.md), and
[connection pool documentation](server/docs/connection_pool.md). The
[trace inference guide](server/docs/trace_inference.md) documents model lineage,
export and deployment.
The [forecast inference guide](server/docs/forecast_inference.md) explains the
prediction contract, exporter, data preparation and Linux validation.

For first-time setup, feature status, desktop usage and troubleshooting, see
the [Chinese user manual](docs/用户使用手册.md).
For the new data workflow, API contract, SQL migrations and VM setup, see
[classification preprocessing](server/docs/classification_preprocessing.md).
Create or migrate the schema and sample data using the ordered scripts in
[`server/sql/README.md`](server/sql/README.md). Classification and forecast now
share one immutable `water_samples` table; `train_data` and `test_data` remain
read-only compatibility views for the existing API.
