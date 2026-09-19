# Inference model directory

Run `tools/export_trace_model.py` to generate:

- `trace_random_forest.onnx`
- `trace_random_forest.json`
- `trace_random_forest.golden.json`

These classification files are now included. ONNX Runtime / sklearn parity
passed for 128 sampled float32 inputs, with identical labels and maximum
probability error `6.556510925292969e-7`. The manifest records the model SHA-256.
The fitted scaler uses explicit float64 constants and intermediate float32
rounding; do not simplify its graph to an approximate reciprocal multiplication.
The real C++ adapter passed all 128 labels and six-class probability checks,
with maximum normalized probability error `1.33477e-7` against the sklearn golden fixture.

The generated ONNX graph contains both the fitted `StandardScaler` and the
trained `RandomForestClassifier`. The server therefore passes the ten raw
database features to the model in the order documented in the manifest.

Do not place sklearn `.pkl` files in the C++ deployment image. They are Python
training artifacts and are only inputs to the export step.

Run `tools/export_forecast_model.py` to generate:

- `forecast_attention_lstm_h10.onnx`
- `forecast_attention_lstm_h10.json`
- `forecast_attention_lstm_h10.golden.json` (real input/PyTorch output fixture)

These prediction files have been generated and verified. Python/ONNX Runtime
comparison passed on three windows (including saved notebook predictions),
with a maximum physical-unit absolute error of `9.5367431640625e-7`.
The C++ ONNX Runtime adapter passed the golden real-model test, too.
Linux network/database end-to-end deployment has not yet been verified.

The prediction graph owns both fitted scalers, the residual Attention-LSTM and
output inverse standardization: physical float32 `[1,120,10]` -> `[1,10,4]`.
Feature order is NOT the trace classifier's order. See
[forecast inference](../docs/forecast_inference.md).
