"""Inference-only architecture matching prediction_test.ipynb, cell 11.

Module/parameter names deliberately match best_model_four_head_h10_diff.pth.
No training, plotting or notebook execution occurs during export.
"""

import torch
from torch import nn


class Encoder(nn.Module):
    def __init__(self):
        super().__init__()
        self.input_proj = nn.Linear(10, 64)
        self.lstm = nn.LSTM(64, 64, num_layers=2, batch_first=True, dropout=0.2)
        self.layer_norm = nn.LayerNorm(64)
        self.dropout = nn.Dropout(0.2)

    def forward(self, x):
        outputs, state = self.lstm(self.dropout(self.input_proj(x)))
        return self.layer_norm(outputs), state


class HorizonDecoderRNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.input_proj = nn.Linear(18, 64)  # 4 targets + 6 auxiliaries + 8 horizon embedding
        self.lstm = nn.LSTM(64, 64, num_layers=2, batch_first=True, dropout=0.2)
        self.layer_norm = nn.LayerNorm(64)
        self.dropout = nn.Dropout(0.2)
        self.head = nn.Sequential(
            nn.Linear(128, 64), nn.GELU(), nn.Dropout(0.2),
            nn.Linear(64, 32), nn.GELU(), nn.Dropout(0.2), nn.Linear(32, 4))

    def forward(self, dec_in, state, enc_outputs):
        outputs, _ = self.lstm(self.dropout(self.input_proj(dec_in)), state)
        outputs = self.layer_norm(outputs)
        weights = torch.softmax(torch.bmm(outputs, enc_outputs.transpose(1, 2)) / 8.0, dim=-1)
        context = torch.bmm(weights, enc_outputs)
        return self.head(torch.cat([outputs, context], dim=-1))


class DirectMultiHorizonAttentionLSTM(nn.Module):
    def __init__(self):
        super().__init__()
        self.encoder = Encoder()
        self.horizon_embedding = nn.Embedding(10, 8)
        self.decoder = HorizonDecoderRNN()

    def forward(self, src):
        batch = src.shape[0]
        outputs, state = self.encoder(src)
        last_y = src[:, -1, :4].unsqueeze(1).expand(batch, 10, 4)
        aux = src[:, -1, 4:].unsqueeze(1).expand(batch, 10, 6)
        embedding = self.horizon_embedding(torch.arange(10, device=src.device))
        embedding = embedding.unsqueeze(0).expand(batch, 10, 8)
        dec_in = torch.cat([last_y, aux, embedding], dim=-1)
        return last_y + self.decoder(dec_in, state, outputs)


class PhysicalUnitForecast(nn.Module):
    """Include fitted sklearn StandardScalers in the deployed ONNX graph."""
    def __init__(self, model, scaler_y, scaler_aux):
        super().__init__()
        self.model = model
        self.register_buffer("input_mean", torch.cat([
            torch.tensor(scaler_y.mean_, dtype=torch.float64),
            torch.tensor(scaler_aux.mean_, dtype=torch.float64)]))
        self.register_buffer("input_scale", torch.cat([
            torch.tensor(scaler_y.scale_, dtype=torch.float64),
            torch.tensor(scaler_aux.scale_, dtype=torch.float64)]))

    def forward(self, raw_window):
        # sklearn uses float64 CSV values before the Dataset casts to float32.
        scaled = ((raw_window.double() - self.input_mean) / self.input_scale).float()
        prediction = self.model(scaled)
        return (prediction.double() * self.input_scale[:4] + self.input_mean[:4]).float()
