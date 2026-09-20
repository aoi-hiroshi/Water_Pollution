# 分类数据清洗与分析：学习和使用指南

本次实现的是**分类的数据处理链路**，不修改预测 Notebook 或预测清洗规则。
Windows 已编译 Qt、运行 C++ 组件与接口测试；Linux Muduo + 真实 MySQL 联调仍待完成。

## 1. 先看这几个文件

| 文件 | 职责 |
| --- | --- |
| `include/water/domain/classification_data.h` | 请求、结果、十个特征的名称与顺序 |
| `src/analysis/classification_analysis.cpp` | 纯 C++ 数学计算，不碰数据库或网络 |
| `src/service/classification_data_service.cpp` | 调度数据库线程池、分析线程池、保存事务 |
| `src/repository/water_repository.cpp` | 参数化读取样本、保存版本、读取清洗后样本 |
| `src/controller/api_controller.cpp` | JSON 校验、路由与响应序列化 |
| `../WaterLogin/dataservice.cpp` | Qt 发起清洗与分析请求，忽略旧请求结果 |
| `../WaterLogin/datapage.cpp` | 按钮、参数、表格、摘要、版本选择和 PNG 导出 |
| `../WaterLogin/classificationcharts.cpp` | Qt 绘制对比曲线、箱线图、直方图、相关性热力图 |
| `sql/000_base_schema.sql` | 公司和统一原始水质数据表 |
| `sql/001_processing_model_schema.sql` | 处理版本、模型、指标和推理结果表 |
| `sql/002_compatibility_views.sql` | 为现有接口提供 train_data/test_data 只读视图 |
| `sql/100_migrate_legacy_schema.sql` | 旧表一次性无损迁移脚本 |

```text
Qt 分类数据页 → POST /api/v1/data/classification/{操作}
  → Controller：校验参数
  → Service → DbExecutor：连接池借连接、读样本快照、归还连接
  → 分类分析线程池：计算（不发 SQL）
  ├─ 预览/分析：JSON → Qt 绘图和表格
  └─ persist=true：再提交 DB 任务 → 事务保存新版本 → JSON 返回版本号
Qt 溯源页 → dataset + sample_id + cleaning_run_id
  → TraceService：读取指定版本 → 推理线程池 → ONNX → 候选公司排名
```

连接池负责复用数据库连接；DbExecutor 负责把 SQL 任务放到数据库线程池，二者不是同一个对象。
默认分类分析并发 1、排队 4；与推理线程池分开，避免分析任务占满模型推理线程。
服务还限制整条分类处理链路的在途数量（工作线程数 + 队列容量），满载时在读取大数据快照之前拒绝请求，避免数据库队列和 CPU 队列之间堆积大量快照。
停机先关闭请求入口、等待分析服务 DB→CPU→DB 全链路结束，再关闭底层线程池和连接池。

## 2. 清洗规则与 Notebook 的关系

- 空值修复：当前公司、当前数据快照、每个特征分别使用非空值中位数；整列空值不虚构数字。此项是新增规则，Notebook 原来只统计空值。
- 异常清洗：先修复空值；默认前 50 个样本计算滑动 Z-score，阈值 3，标准差为总体标准差；前 50 条不进行异常检测。
- 检测窗口使用未进行异常替换的序列；异常点替换为**前一个已经修复的值**，连续异常可连续保持。
- 原 Notebook 的 `kalman_filter_fill` 更新项实际上为零，退化为上述规则；本实现不宣称实现完整卡尔曼滤波。窗口标准差为零时，与窗口常量不同的值视为异常，相同值不视为异常。
- 新实现按公司隔离，ID 升序处理；原 Notebook 对整个 CSV 顺序处理。因此跨公司边界处不保证清洗结果与旧 CSV 逐点一致，模型上线前应评估这种业务规则变化。
- 中位数使用完整快照，属于**离线清洗**，不要把它当作仪器数据流的因果在线预处理。数据库没有采样时间，ID 顺序不是小时、分钟或严格真实采样顺序。
- 分布：线性插值分位数、Tukey 1.5×IQR 箱线范围、默认 50 个直方图分箱；最右边界计入最后一箱，常量列使用一箱，公司对比共用边界。
- 相关性：Pearson 或 Spearman；每对使用共同非空样本，Spearman 并列值取平均秩；常量/不足 2 条返回 `null`，不是 0；列出 `|r|>0.7` 的特征对。相关性不是因果证明。

参考：[NumPy 分位数定义](https://numpy.org/doc/stable/reference/generated/numpy.quantile.html)、[pandas 成对非空相关性](https://pandas.pydata.org/docs/reference/api/pandas.DataFrame.corr.html)。

## 3. 四个接口

均为 POST，Content-Type 为 application/json，响应仍是 `{code,message,data}`。

| 地址后缀（统一前缀 `/api/v1/data/classification/`） | 功能 | 专用参数 |
| --- | --- | --- |
| `missing` | 空值检测、中位数修复及对比 | `persist` |
| `clean` | 中位数修复 + 滑动 Z-score 异常修复 | `window`、`threshold`、`persist` |
| `distribution` | 直方图、箱线统计、各公司对比 | `feature`、`view`、`bins` |
| `correlation` | 十特征相关矩阵与高相关特征对 | `method` |

公共参数：`company_id` 必填正整数；`dataset` 为 train_data/test_data（默认 train_data）；`cleaning_run_id` 默认 0，表示原始表，正整数表示已保存的版本。
`window` 2～500、`threshold` (0,20]、`bins` 5～100；`method` pearson/spearman；`view` histogram/boxplot/comparison。
特征白名单：temperature/ph/cod/nh3n/tp/water_level/orp/conductivity/dissolved_oxygen/turbidity。
未知参数、错误类型、非整数 ID、越界 ID 会返回 400，而不是隐式转换。
只有 missing/clean 可以保存。公司对比仅支持原始数据，不能拿一个公司的版本冒充所有公司。

```bash
# 默认仅预览，不写数据库。
curl -X POST http://127.0.0.1:8080/api/v1/data/classification/clean \
  -H 'Content-Type: application/json' \
  -d '{"company_id":1,"dataset":"test_data","window":50,"threshold":3}'

# 明确保存独立版本，返回 data.cleaning_run_id。
curl -X POST http://127.0.0.1:8080/api/v1/data/classification/clean \
  -H 'Content-Type: application/json' \
  -d '{"company_id":1,"dataset":"test_data","persist":true}'

# 100仅为版本号示例，替换为真实返回值；样本ID仍是原始表ID。
curl -X POST http://127.0.0.1:8080/api/v1/inference/classify \
  -H 'Content-Type: application/json' \
  -d '{"sample_id":1,"dataset":"test_data","cleaning_run_id":100}'
```

响应提供全量样本统计、修复数量、规则及警告；表格前 50 行、对齐的对比曲线最多 300 点。
50000 行上限、32 个公司组上限是资源保护，超限返回 413；不偷偷截取部分数据冒充全量统计。
曲线是抽样预览，可能没有展示所有异常点。Qt 公司分布图最多画前 12 组，摘要仍提供所有组。
整列空值允许预览，但拒绝保存“推理就绪”版本，返回 422 UNRESOLVED_MISSING_VALUES。
保存前还检查所有清洗值可表示为模型的有限 float32 输入；超范围返回 422 INVALID_TRACE_SAMPLE。
分析队列满返回 503 ANALYSIS_BUSY，数据库获取失败返回 503 DATABASE_UNAVAILABLE。

## 4. 保存版本，而不是破坏原表

`processing_runs` 保存任务类型、范围、父版本、算法、窗口、阈值、样本数和状态。
`processed_samples` 只保存源样本 ID 与处理后的十个特征；原值通过外键回查
`water_samples`，不再重复存一份。
一个事务插入版本及全部样本，每批最多 128 条；任一批失败自动回滚，不产生半成品。
不会 UPDATE train_data/test_data。`cleaning_run_id=0` 始终读取原始表，概览按钮也始终读取原始数据。
已保存版本可以作为后续分析或清洗的输入；版本与公司、数据集绑定，不允许串用。

Qt 保存按钮会**重新读取并计算**，使用刚才预览的参数；如果原数据变化，结果可能与预览不同，确认框会说明。
当前没有幂等键：请求超时后不要反复点击保存，需检查版本表，重试可能产生多个相同版本。
HTTP 断开不会取消已经开始的 SQL/计算；大型数据处理后续应改为后台任务 + 状态查询，而不是无限提高线程数。

## 5. Windows Qt + Linux VM 的落地顺序

1. 将源码（尤其 server、sql、models）复制/同步到 Linux；不要复制 Windows exe/dll 当作 Linux 程序。
2. 迁移原数据库的**表结构和样本数据**；仅创建空表无法分析。可使用 MySQL 官方备份导出/导入方式；不要直接复制运行中数据库的数据目录。
3. 如果是新库，管理员先建库，再依次运行三个安装脚本；如果已有旧表，按 `sql/README.md` 的迁移顺序执行并先备份。

```bash
mysql -u root -p Water_Quality_System < server/sql/000_base_schema.sql
mysql -u root -p Water_Quality_System < server/sql/001_processing_model_schema.sql
mysql -u root -p Water_Quality_System < server/sql/002_compatibility_views.sql
```

脚本使用 IF NOT EXISTS，不自动纠正已有表结构。基础表的字段需与用户手册一致。
管理员给应用账号原表 SELECT 权限，给两个版本表 SELECT + INSERT 权限；运行账号不需要建表或修改原始样本权限。

```sql
GRANT SELECT ON Water_Quality_System.* TO 'water_app'@'localhost';
GRANT INSERT ON Water_Quality_System.processing_runs TO 'water_app'@'localhost';
GRANT INSERT ON Water_Quality_System.processed_samples TO 'water_app'@'localhost';
```

账号与实际连接地址要匹配；不要为了让 Qt 使用系统而公开 MySQL 端口，Qt 只访问 Muduo 的 8080。
4. 安装 Linux C++20/CMake/Muduo/MySQL 客户端开发库/nlohmann_json，推理再加 Linux ONNX Runtime .so。见 [服务端构建说明](muduo_server.md)。
5. `WATER_WITH_MYSQL=ON`、`WATER_WITH_MUDUO=ON`、`WATER_WITH_ONNX=ON` 构建；配置数据库和 WATER_TRACE_MODEL_PATH 后启动。
6. Windows 设置 WATER_API_BASE_URL 指向虚拟机；浏览器访问 `/api/v1/health`，确认分类 available=true，再进入 Qt。
7. 数据页选择分类模式、公司和数据集，先空值预览，再异常清洗预览，确认保存；记下版本号。
8. 填入“分析数据版本”可以分析已保存结果；溯源页填相同数据集、源样本 ID 和版本号，执行分类。

Linux 只需 C++ 推理运行库，不需要 Anaconda、Python、sklearn 或 matplotlib；训练和再次导出仍可在 Windows 完成。
小虚拟机先使用 WATER_ANALYSIS_THREADS=1、WATER_ANALYSIS_QUEUE_CAPACITY=4、ONNX 单线程，按实测再调。
尚无完整鉴权，不要将该演示服务直接暴露到公网。

## 6. 模型与验证边界

分类 ONNX 已从原模型导出，内嵌拟合后的 StandardScaler；输入是按固定顺序的十项物理值，**不能再次标准化**。
128 个分散取样的 float32 输入与 sklearn 标签一致，概率最大误差为 6.556510925292969e-7；golden JSON 用于 C++ 实机复验。
导出器显式保留 StandardScaler 的 float64 常量与每一步 float32 舍入，避免树分裂阈值附近的误路由。
这是相同 float32 输入下的导出一致性校验，不等于实际分类准确率评估，也不等于新公司/仪器数据的泛化保证。
精度背景见 [sklearn-onnx 官方说明](https://onnx.ai/sklearn-onnx/auto_examples/plot_cast_transformer.html)。
新数据清洗策略、模型的训练策略、特征顺序、公司标签需要一致且经独立测试集验证；不会自动重训练或自动切换模型。
目前版本用于学习和演示候选污染源分类，不作为真正环境污染因果溯源的结论。

## 7. 回归检查

Linux 构建开启 WATER_BUILD_TESTS 后运行 `ctest --test-dir build/server --output-on-failure`。
分类数学测试不需要真实数据库；分类 HTTP 流水线使用测试连接，验证版本、事务、背压和停机排空。
真实 MySQL 集成测试需要另行配置测试数据库，不能把模拟连接的通过当作真实数据库通过。

Qt Creator 可单独打开 `WaterLogin/tests/classification_smoke.pro` 运行客户端冒烟检查。
测试启动本机临时 HTTP 模拟服务，只验证客户端请求/界面，不连接你的虚拟机或实际数据库。
离屏运行设置 QT_QPA_PLATFORM=offscreen，Windows Qt5.12 还需设置 QT_QPA_FONTDIR=C:/Windows/Fonts；
测试使用 Fusion 样式，保存确认使用普通 Qt 对话框，避免离屏访问 Windows 原生消息框菜单。
生成的 missing/clean/correlation/boxplot/histogram/comparison PNG 是测试图，不是实际业务分析结果。

本次 Windows 63 组检查通过（组件/HTTP/两套真实 ONNX/Qt 模拟交互），Qt Release 构建通过。
Linux CMake/CTest、SQL迁移、真实 MySQL/Muduo网络联调和目标虚拟机压测尚未执行。
