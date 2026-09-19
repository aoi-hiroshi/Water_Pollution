# 趋势预测：从训练模型到 C++ 服务

## 1. 本次接入哪一版模型

采用 `预测任务2 - 副本/prediction_test.ipynb` 中的 10 步版本，读取：

- `data/best_model_four_head_h10_diff.pth`：模型权重；
- `data/scaler_y.pkl`：四个污染物输入/输出共用的 StandardScaler；
- `data/scaler_aux.pkl`：六个辅助特征的 StandardScaler。

网络结构是历史 Encoder LSTM → 带 horizon embedding 的 Decoder LSTM →
Attention → MLP → 与最后一个污染物观测值相加的残差预测。
隐藏维度 64、两层 LSTM、horizon embedding 维度 8。
文件名中的 `four_head` 不表示四个独立网络；实际 checkpoint 使用同一个四维输出头。
`diff` 是训练损失中的趋势差分项，不是在推理后再次累加差分。

不要混用 `best_model.pth`、`best_model_h10.pth`、`scaler_X.pkl` 或旧的
`feature_info.json`。目录保留了多个实验版本，后者记录的是早期特征工程方案，
不是此次 120×10 → 10×4 的模型协议。

## 2. 数据怎么走

```text
Qt PredictionPage
  → PredictionService / ApiClient
  → POST /api/v1/inference/forecast
  → ApiController：校验 JSON 参数
  → ForecastService
      → DbExecutor 的数据库线程
          → Repository 查公司和最近 120 条同公司记录
          → 把历史数据交给有界推理线程池
      → 推理线程
          → 按预测特征顺序组装窗口
          → ONNX：标准化 → Attention-LSTM → 反标准化
          → 组装历史曲线、四指标预测值、模型版本
  → HttpReply 回到 Muduo 的连接 I/O 线程发送 JSON
  → Qt 自定义绘图、结果表格、CSV 导出
```

分类与预测目前共享一个独立于数据库线程池的有界推理线程池。
默认核心 1、最大 2、队列 128；ONNX 单次运算默认内部线程数为 1。
这种小规模配置用于控制总 CPU 并发，后续可根据实测将两类推理拆成独立资源配额。
任何 Controller 都不在 I/O 线程等待 future 或执行 SQL/LSTM。
推理任务捕获的是历史数据，不是数据库连接，连接由 DB 工作任务结束时自动归还。

`DbExecutor::post(task, on_error)` 能报告取连接之前发生的异常，
避免请求在连接创建/获取失败后一直等待到 HTTP 超时。

## 3. 模型的输入输出

输入：float32 `[1,120,10]`，从最早到最新排列的清洗后物理数值。
输出：float32 `[1,10,4]`，反标准化后的未来十步物理数值。

| 输入位置 | 数据库字段 | 含义 |
| --- | --- | --- |
| 0 | `cod` | COD |
| 1 | `nh3n` | 氨氮 |
| 2 | `tp` | 总磷 |
| 3 | `turbidity` | 浊度 |
| 4 | `temperature` | 水温 |
| 5 | `ph` | pH |
| 6 | `water_level` | 液位 |
| 7 | `orp` | ORP |
| 8 | `conductivity` | 电导率 |
| 9 | `dissolved_oxygen` | 溶解氧 |

四个输出依次为 COD、氨氮、总磷、浊度。与分类的十特征排列不同！
C++ 和 Qt 都不再次执行 StandardScaler，不加载 `.pkl` 或 `.pth`。
ONNX 自带 `water.forecast.raw.v1` 协议标识；形状或标识不一致会在启动时拒绝加载。

## 4. 导出 ONNX

项目已包含导出的预测模型和数值对照文件，可以直接部署；
下面的导出步骤用于重新生成模型或学习导出过程。

从项目根目录操作，推荐单独的 Python 3.12 环境：

```bash
python3.12 -m venv .venv-forecast
source .venv-forecast/bin/activate
# 不需要 GPU 时先安装 CPU 版，避免下载 CUDA 依赖：
python -m pip install torch==2.5.1 --index-url https://download.pytorch.org/whl/cpu
python -m pip install -r tools/requirements-forecast-export.txt
python tools/export_forecast_model.py
```

Windows 激活方式是 `.venv-forecast\Scripts\Activate.ps1`。
CPU 安装源来自 [PyTorch 官方历史版本安装说明](https://pytorch.org/get-started/previous-versions/#v251)。
导出脚本使用与原训练环境一致的 PyTorch 2.5.1 的 TorchScript ONNX 导出路径
（显式 `dynamo=False`），用于保留这版固定形状 LSTM 的导出行为。
新的 `dynamo=True` 导出器可作为后续迁移工作，但需要重新验证算子支持和数值一致性。
[PyTorch 官方导出说明](https://docs.pytorch.org/tutorials/beginner/onnx/export_simple_model_to_onnx_tutorial.html)。

生成：

```text
server/models/forecast_attention_lstm_h10.onnx
server/models/forecast_attention_lstm_h10.json
server/models/forecast_attention_lstm_h10.golden.json
```

脚本会检查 scaler 的维度、特征排列和统计量，严格加载 checkpoint，
仅提取原 Notebook 的三个模型类作参考，用多个真实清洗数据窗口比较
原 Notebook/sklearn+PyTorch、独立推理模型、封装模型与 ONNX Runtime，
记录模型/权重/scaler 的 SHA-256、版本及最大绝对误差。
数值检查全部通过后才替换目标 ONNX 文件。失败时不会修改原 checkpoint/scaler。
默认不信任旧 `.npy` 结果；如果还要验证 Notebook 保存的历史预测：

```bash
python tools/export_forecast_model.py --check-saved-predictions
```

该选项会使用 Notebook 同样的 train+val 尾部上下文。
失败可能意味着保存结果过期，或权重、清洗数据、scaler 不属于同一次训练。

## 5. 数据库怎样准备

沿用 `company_info`、`train_data`、`test_data` 及用户手册中的十项数值字段。
为预测序列准备一个真实的公司/监测站记录，并赋予其独立 `company_id`。
将预测目录的清洗后 CSV 按原有行顺序导入，对应中文字段映射见上表。
不要导入 `*_scaled.csv`，也不要把不同监测站或分类样本随机拼成历史窗口。
每个窗口必须属于同一监测序列，采样频率和训练时的记录频率一致。

当前没有自动建表/CSV 上传或导入接口。已有分类数据应保留，预测序列应使用
独立的公司/监测站编号，避免覆盖。不要将 `val` 改名为训练数据来声称模型未见过。
测试表不自动补训练/验证上下文：它自己至少需要 120 条历史。
因此服务端测试窗口与 Notebook 第一个带跨分区上下文的窗口不是同一个窗口。

SQL：`WHERE company_id=? [AND id<=?] ORDER BY id DESC LIMIT 120`，
查完后逆序供模型使用。指定截止 ID 必须存在且属于所选公司/数据集，
不能偷偷替换成较早的 ID。`end_sample_id=0` 代表最新窗口。
建议为两张样本表分别建 `(company_id,id)` 联合索引，执行前先检查现有索引：

```sql
SHOW INDEX FROM train_data;
SHOW INDEX FROM test_data;
-- 没有同等索引时再执行：
CREATE INDEX idx_train_company_id ON train_data(company_id, id);
CREATE INDEX idx_test_company_id ON test_data(company_id, id);
```

现有 DB 没有真实采样时间字段。Notebook 的 `timestamp` 可能是人工构造的
1 分钟序列，不能据此认定现场采样就是每分钟一次。因此接口返回
`axis: sample_step`、`sampling_interval_seconds: null`，界面只显示未来步数。
仅靠 ID 不能检测漏采、重复采样、乱序导入或时间间隔；生产接入要补
`station_id/sampled_at`、唯一约束、采样间隔校验和按时间取窗。

原 Notebook 清洗使用整个分区的百分位数、局部双侧邻居与线性插值，
可能引用未来记录。现有模型用于离线演示；高 R² 不能直接作为在线预测效果证明。
正式上线需要改为因果清洗、按时间切分重新训练，并与 persistence 基线比较。

## 6. Linux 部署与接口

安装 Muduo、MySQL 客户端、nlohmann/json 和 ONNX Runtime C/C++ SDK，
按 [Muduo 部署说明](muduo_server.md) 启用 `WATER_WITH_MYSQL/MUDUO/ONNX` 构建。
ONNX Runtime 非系统安装目录需传 `WATER_ONNXRUNTIME_INCLUDE_DIR` 和
`WATER_ONNXRUNTIME_LIBRARY`，运行时也要让动态链接器找到对应 `.so`。
[ONNX Runtime 官方 C++ 接入说明](https://onnxruntime.ai/docs/get-started/with-cpp.html)。

```bash
export WATER_FORECAST_MODEL_PATH="$PWD/server/models/forecast_attention_lstm_h10.onnx"
export WATER_ONNX_INTRA_OP_THREADS=1
# 其他 DB / 网络变量见用户手册
./build/server/water_server
curl http://127.0.0.1:8080/api/v1/inference/forecast/model
curl -X POST http://127.0.0.1:8080/api/v1/inference/forecast \
  -H 'Content-Type: application/json' \
  -d '{"company_id":7,"dataset":"test_data","end_sample_id":0,"horizon":10}'
```

这里的 `7` 只是示例，改成实际预测站点编号。`horizon` 支持 1～10，
模型始终计算十步，服务返回所需前缀，不做递归外推。
响应 `data` 包括公司、数据集、历史起止 ID、lookback、模型版本、
120 条 `history` 和 1～10 条 `predictions`。
每个点包含 `step/cod/nh3n/tp/turbidity`。
历史 step 是窗口内 1～120 的相对位置；预测 step 是未来 1～10。

模型路径未配置或构建未启用 ONNX：`503 INFERENCE_UNAVAILABLE`；
配置了错误路径/错误协议：服务启动失败，修正文件后重启。
`.env.example` 只是示例，不会自动加载；导出前不要设置不存在的模型路径。
其他错误：`400 INVALID_ARGUMENT`、`404 COMPANY_NOT_FOUND/FORECAST_SAMPLE_NOT_FOUND`、
`422 INSUFFICIENT_HISTORY/INVALID_FORECAST_HISTORY`、
`503 DATABASE_UNAVAILABLE/SERVER_BUSY/INFERENCE_BUSY`、`500 INFERENCE_ERROR`。

## 7. 先学哪些源文件

1. `domain/forecast_data.h`：返回什么数据。
2. `inference/forecaster.h`：模型接口与固定输入输出协议。
3. `repository/water_repository.cpp`：怎样从连接池取出的连接查询窗口。
4. `service/forecast_service.cpp`：生产者如何将任务从 DB 队列交给推理队列。
5. `controller/api_controller.cpp`：HTTP JSON 如何进入业务层。
6. `inference/onnx_forecaster.cpp`：Session、Tensor、Run 与模型元数据校验。
7. `WaterLogin/predictionservice.cpp/predictionpage.cpp`：异步请求、页面更新与绘图。
8. `tools/forecast_model.py/export_forecast_model.py`：保留离线模型语义的导出过程。

测试位于 `server/tests/forecast_service_test.cpp`，覆盖顺序、特征排列、
步数截取、缺模型、缺历史、非法特征、缺公司/截止样本、数据库异常、
取连接之前的异常、队列拒绝与非法输出。组件测试不能替代真实模型数值验证
或 Linux Muduo/MySQL 端到端测试。

本次验证：37组组件/业务检查、4组 Controller/JSON 检查、1组 C++
真实 ONNX 模型检查，共42组通过；Qt5.12.8客户端完整编译通过。
导出时验证了真实窗口0、120、9179，与原 Notebook 和保存预测结果一致，
最大绝对误差约 `9.54e-7`。C++黄金样本测试最大误差为0。
这些是 Windows 本地验证，不代表 Linux Muduo/MySQL 联调已完成。
完整记录见 [预测接入开发日志](../../docs/development_logs/2026-09-16-forecast.md)。
