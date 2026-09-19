"""Export the existing 120-row / 10-step Attention-LSTM bundle to ONNX.

Run with Python 3.12 and requirements-forecast-export.txt. Use only trusted
local checkpoint/pickle assets. Failed verification never replaces a model.
"""

import argparse
import ast
import csv
import hashlib
import json
from pathlib import Path
import tempfile

import joblib
import numpy as np
import onnx
import onnxruntime as ort
import torch
import sklearn

from forecast_model import DirectMultiHorizonAttentionLSTM, PhysicalUnitForecast

PROJECT = Path(__file__).resolve().parents[1]
FEATURES = ["化学需氧量(mg/l)", "氨氮(mg/l)", "总磷(mg/l)", "浊度(无)",
            "水温(℃)", "pH值(无量纲)", "液位(无)", "ORP(无)", "电导率(μs/cm)", "溶解氧(mg/l)"]
DB_FEATURES = ["cod", "nh3n", "tp", "turbidity", "temperature", "ph",
               "water_level", "orp", "conductivity", "dissolved_oxygen"]


def read_features(path):
    with path.open(encoding="utf-8-sig", newline="") as source:
        rows = [[float(row[name]) for name in FEATURES] for row in csv.DictReader(source)]
    values = np.asarray(rows, dtype=np.float64)
    if values.ndim != 2 or values.shape[1] != 10 or not np.isfinite(values).all():
        raise ValueError(f"Invalid cleaned feature data: {path}")
    return values


def validate_scaler(scaler, names):
    if getattr(scaler, "n_features_in_", None) != len(names):
        raise ValueError("Scaler dimensions do not match the selected model")
    if list(getattr(scaler, "feature_names_in_", [])) != names:
        raise ValueError("Scaler feature order mismatch; do not use scaler_X.pkl/feature_info.json")
    if not np.isfinite(scaler.mean_).all() or not np.isfinite(scaler.scale_).all() or (scaler.scale_ <= 0).any():
        raise ValueError("Invalid scaler statistics")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def notebook_reference(path, state):
    # Only the three already-inspected model definitions, never training cells,
    # file-writing statements, plotting code or the __main__ example.
    notebook = json.loads(path.read_text(encoding="utf-8"))
    source = "".join(notebook["cells"][11]["source"])
    names = {"Encoder", "HorizonDecoderRNN", "DirectMultiHorizonAttentionLSTM"}
    classes = [node for node in ast.parse(source).body if isinstance(node, ast.ClassDef) and node.name in names]
    if {node.name for node in classes} != names or len(classes) != 3:
        raise ValueError("Selected notebook no longer contains the expected model definitions")
    namespace = {"torch": torch, "nn": torch.nn}
    exec(compile(ast.Module(body=classes, type_ignores=[]), str(path), "exec"), namespace)
    reference = namespace["DirectMultiHorizonAttentionLSTM"](
        input_dim=10, output_dim=4, hid_dim=64, num_layers=2, pred_steps=10,
        dropout=0.2, horizon_emb_dim=8, target_indices=[0, 1, 2, 3],
        use_aux_last=True, use_attention=True, residual_to_last_y=True,
        time_feature_dim=0, use_src_time=False, use_future_time=False)
    reference.load_state_dict(state, strict=True)
    return reference.eval()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-dir", type=Path, default=PROJECT / "预测任务2 - 副本" / "data")
    parser.add_argument("--output", type=Path, default=PROJECT / "server/models/forecast_attention_lstm_h10.onnx")
    parser.add_argument("--version", default="1.0")
    parser.add_argument("--notebook", type=Path, default=PROJECT / "预测任务2 - 副本/prediction_test.ipynb")
    # Opt in to checking saved predictions; notebook outputs may be stale.
    parser.add_argument("--check-saved-predictions", action="store_true")
    args = parser.parse_args()
    if not args.version.strip():
        raise ValueError("Model version must not be empty")
    checkpoint = args.data_dir / "best_model_four_head_h10_diff.pth"
    y_path, aux_path = args.data_dir / "scaler_y.pkl", args.data_dir / "scaler_aux.pkl"
    scaler_y, scaler_aux = joblib.load(y_path), joblib.load(aux_path)
    validate_scaler(scaler_y, FEATURES[:4])
    validate_scaler(scaler_aux, FEATURES[4:])
    torch.set_num_threads(1)
    model = DirectMultiHorizonAttentionLSTM()
    # Strict loading prevents silently pairing a different generation checkpoint.
    state = torch.load(checkpoint, map_location="cpu", weights_only=True)
    model.load_state_dict(state, strict=True)
    model.eval()
    reference = notebook_reference(args.notebook, state)
    wrapped = PhysicalUnitForecast(model, scaler_y, scaler_aux).eval()

    train = read_features(args.data_dir / "train_cleaned.csv")
    val = read_features(args.data_dir / "val_cleaned.csv")
    test = read_features(args.data_dir / "test_cleaned.csv")
    if len(train) < 120 or len(test) < 130:
        raise ValueError("Need at least 120 training and 130 test history rows")
    combined = np.vstack([train, val])
    context = np.vstack([combined[-120:], test])
    indices = sorted(set([0, 120, len(context) - 120 - 10]))
    windows = [context[index:index + 120].astype(np.float32)[None] for index in indices]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="water-forecast-", dir=args.output.parent) as temporary:
        staged = Path(temporary) / "model.onnx"
        # Pinned PyTorch 2.5.1 TorchScript exporter for this fixed-shape LSTM.
        # Keep batch=1 / length=120 fixed; no dynamic axes or autoregressive loop.
        torch.onnx.export(wrapped, torch.from_numpy(windows[0]), str(staged),
                          input_names=["raw_window"], output_names=["predictions"],
                          opset_version=17, dynamo=False)
        graph = onnx.load(staged)
        onnx.helper.set_model_props(graph, {
            "water.forecast.contract": "water.forecast.raw.v1",
            "water.model.name": "attention_lstm_h10", "water.model.version": args.version,
            "water.input.features": json.dumps(DB_FEATURES),
            "water.checkpoint.sha256": digest(checkpoint),
            "water.scaler_y.sha256": digest(y_path), "water.scaler_aux.sha256": digest(aux_path)})
        onnx.checker.check_model(graph)
        onnx.save(graph, staged)
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        session = ort.InferenceSession(str(staged), sess_options=options, providers=["CPUExecutionProvider"])
        saved = None
        if args.check_saved_predictions:
            saved = np.load(args.data_dir / "direct_mh_four_head_h10_diff_preds_denorm.npy", allow_pickle=False)
        errors = []
        with torch.no_grad():
            for index, window in zip(indices, windows):
                scaled = np.concatenate([
                    scaler_y.transform(window[0, :, :4]),
                    scaler_aux.transform(window[0, :, 4:])], axis=1).astype(np.float32)
                tensor = torch.from_numpy(scaled[None])
                expected_std = reference(tensor).numpy()
                np.testing.assert_allclose(model(tensor).numpy(), expected_std, rtol=1e-5, atol=1e-5)
                expected = scaler_y.inverse_transform(expected_std.reshape(-1, 4)).reshape(1, 10, 4)
                wrapper_result = wrapped(torch.from_numpy(window)).numpy()
                actual = session.run(["predictions"], {"raw_window": window})[0]
                if actual.shape != (1, 10, 4) or not np.isfinite(actual).all():
                    raise ValueError("Unexpected ONNX output shape or NaN/Inf")
                np.testing.assert_allclose(wrapper_result, expected, rtol=1e-4, atol=1e-4)
                np.testing.assert_allclose(actual, expected, rtol=1e-4, atol=1e-4)
                if saved is not None:
                    np.testing.assert_allclose(actual[0], saved[index], rtol=1e-4, atol=1e-4)
                errors.append(float(np.max(np.abs(actual - expected))))
        golden_input = windows[0].reshape(-1).tolist()
        with torch.no_grad():
            golden_output = wrapped(torch.from_numpy(windows[0])).numpy().reshape(10, 4).tolist()
        model_hash = digest(staged)
        # Replace only once every numerical check has succeeded.
        staged.replace(args.output)
    manifest = {
        "name": "attention_lstm_h10", "version": args.version, "contract": "water.forecast.raw.v1",
        "source_notebook": "预测任务2 - 副本/prediction_test.ipynb (cells 9-11, 13)",
        "checkpoint": checkpoint.name, "checkpoint_sha256": digest(checkpoint),
        "scaler_y_sha256": digest(y_path), "scaler_aux_sha256": digest(aux_path),
        "model_sha256": model_hash, "lookback": 120, "horizon": 10,
        "torch_version": torch.__version__, "onnx_version": onnx.__version__,
        "onnxruntime_version": ort.__version__, "sklearn_version": sklearn.__version__, "opset": 17,
        "input_shape": [1, 120, 10], "output_shape": [1, 10, 4],
        "input_features": DB_FEATURES, "targets": DB_FEATURES[:4],
        "input": "cleaned physical measurements", "output": "physical measurements",
        "sampling_interval_seconds": None, "verified_window_indices": indices,
        "max_absolute_errors": errors, "saved_predictions_checked": saved is not None}
    args.output.with_suffix(".json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    golden = {"model_sha256": model_hash, "input_shape": [1, 120, 10], "input": golden_input,
              "expected": golden_output, "rtol": 1e-4, "atol": 1e-4}
    args.output.with_suffix(".golden.json").write_text(json.dumps(golden, indent=2), encoding="utf-8")
    print(f"Verified and exported: {args.output}")
    print(f"Physical-unit max absolute errors: {errors}")


if __name__ == "__main__":
    main()
