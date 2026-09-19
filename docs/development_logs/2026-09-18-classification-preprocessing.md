# 2026-09-18：分类数据清洗、分析与清洗版本分类

## 需求与范围

将“分类任务”的清洗与分析接入 Qt→Muduo→Controller→Service 的 C++ 框架。
保留原始训练文件和预测业务，不恢复 Flask，不改 QSS。
本次不连接用户的 Linux 虚拟机，不读取或修改真实数据库，不自动训练新模型。

## 业务规则核对

读取 classification.ipynb 的代码单元，而不是运行 Notebook。
原代码按前 50 条窗口、总体标准差和阈值 3 检测异常。
其所谓 Kalman 更新项 `filled[i-1]-x_pred` 为零，实际是前值保持，故明确命名为滑动 Z-score/forward hold。
新增中位数空值修复；整列空值不补虚构值。
按公司隔离、按 ID 升序；这与原 Notebook 全 CSV 的跨公司处理不同，需要后续独立测试集评估。
不将 ID 当作真实时间，不将全快照中位数当作因果在线处理，不将分类概率当作因果溯源证明。

## 实现决策

- 数学计算为纯 C++ 模块，独立于网络和数据库。
- 四个 POST 接口：missing、clean、distribution、correlation。
- 数据读取经 DbExecutor；CPU 分析经独立的小有界线程池。
- 整条链路在途数量有界，读取大快照前即可拒绝超载请求。
- 不在 Muduo IO 回调运行 SQL、清洗或推理；不持有 DB 连接等待 CPU 队列。
- 默认仅预览；明确 persist=true 才使用事务保存独立清洗版本。
- 原表不 UPDATE；新版本保存范围、规则、参数、父版本、前后特征和源样本 ID。
- 每批最多 128 条插入，一个事务覆盖所有批次，失败自动回滚。
- 清洗版本绑定公司和数据集；分析可读取版本，TraceService 可读取该版本的指定源样本。
- 保存前拒绝整列 NULL、非法值和超过模型 float32 范围的值。
- 全量统计上限 50000 条；公司组上限 32；HTTP 表格前 50 条，图表对齐抽样最多 300 点。
- 分布使用线性分位数、Tukey 箱线和共用直方图边界；相关性使用成对非空 Pearson/Spearman，并列秩取平均。
- Qt 本地绘图、PNG 导出，不要求 Linux 安装 Python 绘图库。
- Qt 处理时禁用会改变数据范围的控件；数据服务忽略旧请求的响应；NULL 显示为空值/NA，不当作 0。
- 修正保存操作名称与预览成员的别名问题，避免清空状态时把请求路由也清空。
- 停机先关闭入口，等待已接收的 DB→CPU→DB 流水线完成，再关闭底层执行器。
- WaterService/TraceService 补连接取得之前的异常回调，避免客户端只能等超时。

## 分类模型导出

使用已有 ISR_ Python 3.10 / sklearn 1.6.1 / NumPy 2.1.2，在项目临时虚拟环境安装
ONNX 1.17.0、skl2onnx 1.18.0、ONNX Runtime 1.20.1；未修改原 Conda 环境。
从保存的 StandardScaler、RandomForest、LabelEncoder 导出，没有重新训练权重。

初始普通转换在 128 个样本上出现约 0.01 的概率差，严格校验未通过。
排查发现 float32 标准化近树分裂阈值的精度问题；显式编码 float64 常量、Sub/Div 之后各自回到 float32 的舍入步骤。
不放宽误差阈值，图校验和数值校验通过后才输出 ONNX、manifest、golden。

- Python ONNX/sklearn：128 条分散取样，标签一致，最大概率误差 6.556510925292969e-7。
- C++ ONNX/sklearn golden：128 条标签一致，归一化后最大概率误差 1.33477e-7。
- C++ 预测模型回归：真实 Attention-LSTM golden 测试通过，最大误差 0。
- Windows 测试运行库须与头文件匹配；曾被系统旧 ORT DLL 抢先加载，改用测试程序旁的官方匹配 DLL 后通过。Linux 同样需要核对 .so 搜索路径。

## 验证

GCC C++20 使用 -Wall -Wextra -Wpedantic -Werror 编译。
九个组件/HTTP 测试程序共 56 组检查通过：
线程池8、连接池9、HTTP5、水数据服务2、分类服务2、预测服务11、预测API4、分类分析6、分类HTTP流水线9。
新增覆盖：中位数/整列NULL、连续异常与零方差、分位数/箱外值/共用分箱、成对非空/并列Spearman、
资源上限、类型与整数溢出、版本范围、无原表写入、批量事务/回滚、空数据/非法数据、
连接取得前失败、CPU背压、在途事务停机排空和计算等待期间不占用 DB lease。
两个真实 C++ ONNX 适配器检查也通过。
Qt 5.12.8 / MinGW 7.3 客户端 Release 构建通过。

Qt 模拟 HTTP 冒烟测试代码位于 WaterLogin/tests/classification_smoke.pro，
用于验证按钮、保存确认、版本传递和六种图表，不代表 Linux 网络/数据库端到端验证。
离屏测试的5组检查全部通过：公司/概览与NULL显示、四个分析按钮的HTTP参数、
保存确认与返回版本选择、分类版本传递、六种图表绘制与PNG导出。
检查导出图像，中文标签、NA热力图和曲线/分布布局可读。
曾在 QMessageBox 环节异常，调试调用栈明确落在 Qt5.12.8 的
qt_getWindowsSystemMenu / QMessageBox::showEvent；离屏插件不能提供 Windows 菜单接口。
保存确认改为通用 QDialog + QDialogButtonBox（默认不保存），复验通过。
测试字体通过 QT_QPA_FONTDIR 指定系统字体，Fusion 样式只用于测试。
合计63组组件/API/真实模型/Qt冒烟检查通过；不把128个数值样本算作128组独立测试。

本机没有 CMake/真实 MySQL/Muduo Linux 运行环境，未执行 CTest、SQL 实机迁移或虚拟机压测。
SQL 事务的自动化检查使用测试连接，真实 MySQL 语法/权限/外键仍须在 Linux 验证。

## 后续验收

1. 同步源码和两套 ONNX 到 Linux，在目标平台重新构建。
2. 迁移基础表及公司/样本数据，运行清洗版本 SQL，按最小权限配置账号。
3. 健康接口、公司列表、清洗预览、保存、版本分析、版本分类逐项实机检查。
4. 对按公司隔离的清洗规则与已有模型进行独立测试集评估。
5. 原 Flask CSV 上传/导入入口尚未迁移为 C++ API；已有数据暂通过数据库工具迁移/导入。
6. 后续补幂等保存、任务状态、日志持久化、仪器采样时间/站点协议和 Docker；不在本次扩展范围内。
