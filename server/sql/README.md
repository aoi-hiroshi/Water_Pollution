# 数据库脚本使用说明

项目只使用一个 MySQL 数据库，命名为 `Water_Quality_System`。分类与预测共用公司表和原始水质表，避免维护两份相同字段的数据。Linux 上数据库名可区分大小写，所有命令应保持该写法。

## 全新安装

```sql
CREATE DATABASE IF NOT EXISTS Water_Quality_System
  CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;
USE Water_Quality_System;
SOURCE server/sql/000_base_schema.sql;
SOURCE server/sql/001_processing_model_schema.sql;
SOURCE server/sql/002_compatibility_views.sql;
```

## 已存在旧表

先用 `mysqldump` 备份，再依次执行 `000_base_schema.sql`、`001_processing_model_schema.sql`、`100_migrate_legacy_schema.sql`。迁移脚本会保留旧表，但改名为 `legacy_*`；确认行数和接口无误后再决定是否删除。迁移脚本只能执行一次。

## 数据导入映射

| 数据来源 | company_id | data_split | sampled_at | sample_index |
|---|---:|---|---|---|
| 分类 `train_data.csv` | CSV 标签 1～6 | `train` | `NULL` | 同一公司内按 CSV 顺序递增 |
| 分类 `test_data.csv` | CSV 标签 1～6 | `test` | `NULL` | 同一公司内按 CSV 顺序递增 |
| 预测 `train.csv` | 7 | `train` | 原始“数据时间” | 按时间递增 |
| 预测 `val.csv` | 7 | `validation` | 原始“数据时间” | 按时间递增 |
| 预测 `test.csv` | 7 | `test` | 原始“数据时间” | 按时间递增 |

不要把 `scaled`、`featured`、`.npy`、`.pkl` 或 Notebook 额外生成的时间列导入数据库。缩放参数已经封装在 ONNX 模型中。

`company_info` 保持原有七字段业务结构：`company_id`、`company_name`、`company_code`、`task_type`、`location`、`description`、`created_at`。公司 1～6 使用 `task_type='trace'`，公司 7 使用 `task_type='forecast'`。采样间隔由数据集/模型契约管理，不在公司表里重复保存。

## 表的职责

- `company_info`：7 家公司的静态信息和业务类型。
- `water_samples`：唯一原始事实表，只追加、不覆盖。
- `processing_runs`：每次清洗/去噪的参数、版本和状态。
- `processed_samples`：处理值；原值通过 `source_sample_id` 回查。
- `model_registry`：ONNX 模型版本及输入输出契约。
- `model_metrics`：分类或预测的通用性能指标。
- `classification_results`：分类推理结果。
- `forecast_runs` / `forecast_points`：一次预测及其 1～10 分钟预测点。
- `train_data` / `val_data` / `test_data`：映射 `train` / `validation` / `test` 的只读兼容视图，不是重复数据；预测预处理使用三者，分类仍只使用训练集和测试集。

运行期账号只需这些表的 `SELECT`、`INSERT` 权限；建表、迁移和删表使用单独的管理员账号。
