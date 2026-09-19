# Muduo API Server

## Request path

```text
Qt UI
  -> ApiClient / QNetworkAccessManager
  -> HTTP/1.1 + JSON
  -> MuduoHttpServer
  -> HttpRouter
  -> ApiController
  -> WaterService
  -> WaterRepository
  -> DbExecutor
```

`MuduoHttpServer` is built on `muduo::net::TcpServer`. Each TCP connection owns
an incremental HTTP decoder and permits one in-flight request. Additional
pipelined bytes remain buffered until the current response is sent, preserving
HTTP response order.

Route handlers receive a thread-safe `HttpReply`. A service callback can invoke
it from a database worker; the Muduo adapter uses `EventLoop::queueInLoop()` to
send the response from the connection's IO loop. No controller waits on a
future and no blocking SQL runs in an EventLoop.

## API v1

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/api/v1/health` | Process health and classification/forecast-model availability |
| GET | `/api/v1/companies` | Company list |
| GET | `/api/v1/data/overview?company_id=1&dataset=train_data&limit=10` | Preview and aggregate statistics |
| POST | `/api/v1/inference/classify` | Random Forest pollution-source classification |
| GET | `/api/v1/inference/forecast/model` | Forecast model availability and window contract |
| POST | `/api/v1/inference/forecast` | 120-row -> four-target, up to ten-step forecast |
| POST | `/api/v1/data/classification/missing` | Median missing-value repair preview/version save |
| POST | `/api/v1/data/classification/clean` | Sliding Z-score and forward-hold repair |
| POST | `/api/v1/data/classification/distribution` | Histogram, box statistics, company comparison |
| POST | `/api/v1/data/classification/correlation` | Pearson / Spearman matrix |

Every response uses this envelope:

```json
{
  "code": 200,
  "message": "success",
  "data": {}
}
```

Errors additionally contain a stable `error` code. A full database worker
queue and a connection-acquisition timeout are returned as HTTP 503.

The first codec version supports HTTP/1.0 and HTTP/1.1, Content-Length bodies,
keep-alive, URL query decoding, header/body limits and request deadlines. It
rejects chunked request bodies. Qt's JSON requests use Content-Length.

## Linux dependencies

Install a C++20 compiler, CMake, MySQL client development files,
`nlohmann/json`, and Muduo (`muduo_net` and `muduo_base`). Build Muduo from its
official source if the distribution does not package it. Pollution trace
inference additionally requires the ONNX Runtime C/C++ package.

```bash
sudo apt update
sudo apt install -y build-essential cmake libboost-dev \
  libmysqlclient-dev nlohmann-json3-dev

cmake -S server -B build/server \
  -DCMAKE_BUILD_TYPE=Debug \
  -DWATER_WITH_MYSQL=ON \
  -DWATER_WITH_MUDUO=ON \
  -DWATER_WITH_ONNX=ON \
  -DWATER_BUILD_TESTS=ON
cmake --build build/server -j
ctest --test-dir build/server --output-on-failure
```

If Muduo or MySQL is installed in a non-standard prefix, pass
`CMAKE_PREFIX_PATH`, `WATER_MUDUO_INCLUDE_DIR`, `WATER_MUDUO_NET_LIBRARY`,
`WATER_MUDUO_BASE_LIBRARY`, `WATER_MYSQL_INCLUDE_DIR`, and
`WATER_MYSQL_LIBRARY` to CMake. For ONNX Runtime use
`WATER_ONNXRUNTIME_INCLUDE_DIR` and `WATER_ONNXRUNTIME_LIBRARY`. Omit
`WATER_WITH_ONNX=ON` only when both inference endpoints may remain unavailable.

## Configuration

The server reads configuration from environment variables. Secrets are never
compiled into the executable.

```bash
export WATER_SERVER_HOST=0.0.0.0
export WATER_SERVER_PORT=8080
export WATER_IO_THREADS=2

export WATER_DB_HOST=127.0.0.1
export WATER_DB_PORT=3306
export WATER_DB_USER=water_app
export WATER_DB_PASSWORD='replace-me'
export WATER_DB_NAME=water_quality_system
export WATER_DB_POOL_MIN=2
export WATER_DB_POOL_MAX=8
export WATER_DB_QUEUE_CAPACITY=512

export WATER_TRACE_MODEL_PATH="$PWD/server/models/trace_random_forest.onnx"
export WATER_FORECAST_MODEL_PATH="$PWD/server/models/forecast_attention_lstm_h10.onnx"
export WATER_INFERENCE_CORE_THREADS=1
export WATER_INFERENCE_MAX_THREADS=2
export WATER_INFERENCE_QUEUE_CAPACITY=128
export WATER_ONNX_INTRA_OP_THREADS=1

./build/server/water_server
```

Smoke test:

```bash
curl http://127.0.0.1:8080/api/v1/health
curl http://127.0.0.1:8080/api/v1/companies
curl 'http://127.0.0.1:8080/api/v1/data/overview?company_id=1&dataset=train_data&limit=10'
curl -X POST http://127.0.0.1:8080/api/v1/inference/classify \
  -H 'Content-Type: application/json' \
  -d '{"sample_id":1,"dataset":"test_data"}'
```

For a Qt client running on Windows and a server running in a Linux virtual
machine, set the VM address before launching Qt:

```powershell
$env:WATER_API_BASE_URL = "http://192.168.1.100:8080"
```

## Extension points

Trace classification is implemented as a separate service with its own bounded
CPU worker pool. See [trace inference](trace_inference.md) for model export,
configuration and request details. Forecasting is implemented by `ForecastService`
using the same bounded inference pool. See [forecast inference](forecast_inference.md)
for the model contract, export, database history requirements and endpoint usage.

Classification preprocessing has its own bounded CPU pool (default one worker,
four queued tasks). See [classification preprocessing](classification_preprocessing.md)
for immutable versions, SQL migrations, permissions and Qt use. Do not run CPU
analysis in a Muduo IO callback or retain a database lease across model/CPU work.
