# Cell 1: 原始数据展示
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from scipy import stats
from statsmodels.tsa.seasonal import STL
from statsmodels.tsa.stattools import acf, pacf
import shap
from sklearn.preprocessing import StandardScaler, MinMaxScaler
from sklearn.ensemble import RandomForestRegressor
from sklearn.decomposition import PCA
from sklearn.feature_selection import SelectKBest, f_regression, mutual_info_regression
import warnings
warnings.filterwarnings('ignore')

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

# 读取数据
train_data = pd.read_csv('./data/train.csv')
val_data = pd.read_csv('./data/val.csv')
test_data = pd.read_csv('./data/test.csv')

print("数据集信息:")
print(f"训练集: {train_data.shape}")
print(f"验证集: {val_data.shape}")
print(f"测试集: {test_data.shape}")

# 假设有时间列，如果没有则创建
if 'timestamp' not in train_data.columns:
    start_time = pd.to_datetime('2025-01-09 16:59:00')
    train_data['timestamp'] = pd.date_range(start=start_time, periods=len(train_data), freq='1min')
    val_start = train_data['timestamp'].iloc[-1] + pd.Timedelta(minutes=1)
    val_data['timestamp'] = pd.date_range(start=val_start, periods=len(val_data), freq='1min')
    test_start = val_data['timestamp'].iloc[-1] + pd.Timedelta(minutes=1)
    test_data['timestamp'] = pd.date_range(start=test_start, periods=len(test_data), freq='1min')

feature_cols = ['水温(℃)', 'pH值(无量纲)', '化学需氧量(mg/l)', '氨氮(mg/l)',
                '总磷(mg/l)', '液位(无)', 'ORP(无)', '电导率(μs/cm)',
                '溶解氧(mg/l)', '浊度(无)']

print("\n数据时间范围:")
print(f"开始时间: {train_data['timestamp'].min()}")
print(f"结束时间: {test_data['timestamp'].max()}")
print(f"总时长: {test_data['timestamp'].max() - train_data['timestamp'].min()}")

print("\n训练集统计信息:")
print(train_data[feature_cols].describe())

# 可视化原始数据趋势
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
for i, col in enumerate(feature_cols):
    sample_data = train_data.iloc[:80000]
    axes[i].plot(sample_data['timestamp'], sample_data[col])
    axes[i].set_title(f'{col}时间序列')
    axes[i].tick_params(axis='x', rotation=45)
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

#cell2 数据清洗
from scipy.interpolate import interp1d


def effective_outlier_detection(data, feature_name):
    """
    真正有效的异常点检测 - 专门对付明显的尖峰异常
    """
    outliers = np.zeros(len(data), dtype=bool)

    # 方法1: 更激进的百分位数方法
    lower_bound = np.percentile(data, 1)  # 1%分位数
    upper_bound = np.percentile(data, 99)  # 99%分位数
    outliers_percentile = (data < lower_bound) | (data > upper_bound)

    # 方法2: 检测极端跳跃
    if len(data) > 1:
        # 计算相邻点的差值
        diffs = np.abs(np.diff(data))
        diff_threshold = np.percentile(diffs, 90) * 5  # 90%分位数的5倍
        large_jumps = np.concatenate([[False], diffs > diff_threshold])

        # 如果一个点前后都有大跳跃，很可能是异常点
        jump_outliers = np.zeros_like(data, dtype=bool)
        for i in range(1, len(data) - 1):
            if large_jumps[i] and large_jumps[i + 1]:
                jump_outliers[i] = True
    else:
        jump_outliers = np.zeros_like(data, dtype=bool)

    # 方法3: 基于局部中位数的检测（对付明显突出的点）
    window_size = min(20, len(data) // 5)
    median_outliers = np.zeros_like(data, dtype=bool)

    for i in range(len(data)):
        start = max(0, i - window_size // 2)
        end = min(len(data), i + window_size // 2 + 1)
        local_data = data[start:end]

        # 排除当前点计算局部中位数和MAD
        local_data_no_current = np.concatenate([local_data[:i - start], local_data[i - start + 1:]])
        if len(local_data_no_current) > 0:
            local_median = np.median(local_data_no_current)
            mad = np.median(np.abs(local_data_no_current - local_median))

            # 如果当前点偏离局部中位数太远，标记为异常
            if mad > 0:
                z_score = np.abs(data[i] - local_median) / (mad * 1.4826)  # MAD转标准差
                if z_score > 3:  # 3个标准差
                    median_outliers[i] = True

    # 方法4: 针对特定特征的规则
    feature_outliers = np.zeros_like(data, dtype=bool)

    if '总磷' in feature_name:
        # 总磷正常范围通常0-10mg/L，超过50的肯定异常
        feature_outliers = data > 20
    elif '化学需氧量' in feature_name:
        # COD正常0-100，超过200的可能异常
        feature_outliers = data > 150
    elif 'pH' in feature_name:
        # pH正常6-9，其他的异常
        feature_outliers = (data < 4) | (data > 10)
    elif '浊度' in feature_name:
        # 浊度正常0-50，超过100的异常
        feature_outliers = data > 200
    elif '氨氮' in feature_name:
        # 氨氮正常0-50，超过200的异常
        feature_outliers = data > 100

    # 综合所有方法
    outliers = outliers_percentile | jump_outliers | median_outliers | feature_outliers

    return outliers


def simple_interpolation_fill(data, outliers):
    """
    简单线性插值填充异常值
    """
    filled_data = data.copy()

    if not np.any(outliers):
        return filled_data

    normal_indices = np.where(~outliers)[0]
    outlier_indices = np.where(outliers)[0]

    if len(normal_indices) < 2:
        filled_data[outliers] = np.median(data[~outliers]) if len(data[~outliers]) > 0 else np.median(data)
        return filled_data

    # 线性插值
    interp_func = interp1d(normal_indices, data[normal_indices],
                           kind='linear', bounds_error=False,
                           fill_value=(data[normal_indices[0]], data[normal_indices[-1]]))

    filled_data[outliers] = interp_func(outlier_indices)

    return filled_data


print("开始数据清洗...")

# 检查空值
print("空值检查:")
print(f"训练集空值: {train_data[feature_cols].isnull().sum().sum()}")
print(f"验证集空值: {val_data[feature_cols].isnull().sum().sum()}")
print(f"测试集空值: {test_data[feature_cols].isnull().sum().sum()}")

# 异常值检测和填充
train_cleaned = train_data.copy()
val_cleaned = val_data.copy()
test_cleaned = test_data.copy()

outlier_stats = {}
for col in feature_cols:
    print(f"处理特征: {col}")

    # 训练集异常值处理
    outliers_train = effective_outlier_detection(train_data[col].values, col)
    train_cleaned[col] = simple_interpolation_fill(train_data[col].values, outliers_train)

    # 验证集异常值处理
    outliers_val = effective_outlier_detection(val_data[col].values, col)
    val_cleaned[col] = simple_interpolation_fill(val_data[col].values, outliers_val)

    # 测试集异常值处理
    outliers_test = effective_outlier_detection(test_data[col].values, col)
    test_cleaned[col] = simple_interpolation_fill(test_data[col].values, outliers_test)

    outlier_stats[col] = {
        'train_outliers': outliers_train.sum(),
        'val_outliers': outliers_val.sum(),
        'test_outliers': outliers_test.sum()
    }

print("\n异常值统计:")
for col, stats in outlier_stats.items():
    total_outliers = stats['train_outliers'] + stats['val_outliers'] + stats['test_outliers']
    print(
        f"{col}: 训练集{stats['train_outliers']}个, 验证集{stats['val_outliers']}个, 测试集{stats['test_outliers']}个, 总计{total_outliers}个")

# 重点展示几个问题特征的清洗效果
problem_features = ['总磷(mg/l)', '化学需氧量(mg/l)', '浊度(无)', 'pH值(无量纲)']
fig, axes = plt.subplots(len(problem_features), 1, figsize=(15, 3 * len(problem_features)))
if len(problem_features) == 1:
    axes = [axes]
plt.rcParams.update({
    'font.size': 14,  # 默认字体大小
    'axes.titlesize': 18,  # 标题字体大小
    'axes.labelsize': 14,  # 轴标签字体大小
    'xtick.labelsize': 15,  # x轴刻度标签字体大小
    'ytick.labelsize': 15,  # y轴刻度标签字体大小
    'legend.fontsize': 14,  # 图例字体大小
})
for i, col in enumerate(problem_features):
    sample_size = 80000

    # 绘制原始数据和清洗后数据
    axes[i].plot(train_data[col].iloc[:sample_size], color='red', alpha=0.8, linewidth=1, label='原始数据（含异常点）')
    axes[i].plot(train_cleaned[col].iloc[:sample_size], color='blue', alpha=0.8, linewidth=1, label='清洗后数据')

    # 标记异常点
    outliers_sample = effective_outlier_detection(train_data[col].iloc[:sample_size].values, col)
    outlier_indices = np.where(outliers_sample)[0]
    if len(outlier_indices) > 0:
        axes[i].scatter(outlier_indices, train_data[col].iloc[outlier_indices],
                        color='orange', s=20, label=f'检测到的异常点({len(outlier_indices)}个)', zorder=5)

    axes[i].set_title(f'{col} - 异常检测与清洗效果')
    axes[i].set_ylabel('数值')
    axes[i].legend()
    axes[i].grid(True, alpha=0.3)

    # 显示清洗前后的数值范围
    orig_min, orig_max = train_data[col].iloc[:sample_size].min(), train_data[col].iloc[:sample_size].max()
    clean_min, clean_max = train_cleaned[col].iloc[:sample_size].min(), train_cleaned[col].iloc[:sample_size].max()
    axes[i].text(0.02, 0.98, f'原始范围: [{orig_min:.2f}, {orig_max:.2f}]\n清洗后: [{clean_min:.2f}, {clean_max:.2f}]',
                 transform=axes[i].transAxes, verticalalignment='top',
                 bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8))

plt.tight_layout()
plt.show()

# 全部特征的清洗前后对比
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
for i, col in enumerate(feature_cols):
    sample_idx = slice(0, 80000)
    axes[i].plot(train_data[col].iloc[sample_idx], alpha=0.6, label='原始数据', linewidth=1, color='red')
    axes[i].plot(train_cleaned[col].iloc[sample_idx], alpha=0.8, label='清洗后', linewidth=1, color='blue')
    axes[i].set_title(f'{col}')
    axes[i].legend()
    axes[i].grid(True, alpha=0.3)

plt.tight_layout()
plt.show()

# 清洗效果评估
print("\n清洗效果评估:")
for col in feature_cols:
    if outlier_stats[col]['train_outliers'] > 0:
        original_range = np.max(train_data[col]) - np.min(train_data[col])
        cleaned_range = np.max(train_cleaned[col]) - np.min(train_cleaned[col])
        reduction = (original_range - cleaned_range) / original_range * 100

        original_std = np.std(train_data[col])
        cleaned_std = np.std(train_cleaned[col])
        std_reduction = (original_std - cleaned_std) / original_std * 100

        print(f"{col}: 数据范围缩小 {reduction:.1f}%, 标准差减少 {std_reduction:.1f}%")
    else:
        print(f"{col}: 未检测到异常点")

# 保存清洗后的数据
train_cleaned.to_csv('./data/train_cleaned.csv', index=False, encoding='utf-8-sig')
val_cleaned.to_csv('./data/val_cleaned.csv', index=False, encoding='utf-8-sig')
test_cleaned.to_csv('./data/test_cleaned.csv', index=False, encoding='utf-8-sig')
print("清洗后数据已保存")

#cell3 数据分布
print("数据分布分析...")

# 合并所有数据进行整体分析
all_data = pd.concat([train_cleaned[feature_cols], val_cleaned[feature_cols], test_cleaned[feature_cols]], ignore_index=True)

# 直方图
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
for i, col in enumerate(feature_cols):
    axes[i].hist(all_data[col], bins=50, alpha=0.7, edgecolor='black')
    axes[i].set_title(f'{col}分布')
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

# 箱形图
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
for i, col in enumerate(feature_cols):
    axes[i].boxplot(all_data[col])
    axes[i].set_title(f'{col}箱形图')
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

# 分位点统计
print("各特征分位点统计:")
quantiles = [0.01, 0.05, 0.25, 0.5, 0.75, 0.95, 0.99]
for col in feature_cols:
    q_values = np.quantile(all_data[col], quantiles)
    print(f"{col}: " + " | ".join([f"Q{int(q*100)}:{v:.2f}" for q, v in zip(quantiles, q_values)]))

# 时序分解示例（选择化学需氧量）
sample_col = '化学需氧量(mg/l)'
sample_data = train_cleaned[sample_col].iloc[:10080].values  # 取7天数据进行分解

fig, axes = plt.subplots(4, 1, figsize=(15, 10))
plt.rcParams.update({
    'font.size': 14,          # 默认字体大小
    'axes.titlesize': 14,     # 标题字体大小
    'axes.labelsize': 14,     # 轴标签字体大小
    'xtick.labelsize': 14,    # x轴刻度标签字体大小
    'ytick.labelsize': 14,    # y轴刻度标签字体大小
    'legend.fontsize': 14,    # 图例字体大小
})
# 原始序列
axes[0].plot(sample_data)
axes[0].set_title(f'{sample_col} - 原始序列')
axes[0].grid(True, alpha=0.3)

# 移动平均趋势
window = 1440  # 24小时窗口
trend = pd.Series(sample_data).rolling(window=window, center=True).mean()
axes[1].plot(trend)
axes[1].set_title('趋势（24小时移动平均）')
axes[1].grid(True, alpha=0.3)

# 日内周期模式
daily_pattern = []
for hour in range(24):
    hour_data = []
    for day in range(len(sample_data) // 1440):
        idx = day * 1440 + hour * 60
        if idx < len(sample_data):
            hour_data.append(sample_data[idx])
    daily_pattern.append(np.mean(hour_data) if hour_data else 0)

axes[2].plot(range(24), daily_pattern, 'o-')
axes[2].set_title('日内模式（小时平均）')
axes[2].set_xlabel('小时')
axes[2].grid(True, alpha=0.3)

# 残差
detrended = sample_data[:len(trend)] - trend.fillna(method='bfill').fillna(method='ffill')
axes[3].plot(detrended)
axes[3].set_title('去趋势残差')
axes[3].grid(True, alpha=0.3)

plt.tight_layout()
plt.show()


#cell4 数据分析
print("相关性分析...")
plt.rcParams.update({
    'font.size': 14,          # 默认字体大小
    'axes.titlesize': 14,     # 标题字体大小
    'axes.labelsize': 14,     # 轴标签字体大小
    'xtick.labelsize': 14,    # x轴刻度标签字体大小
    'ytick.labelsize': 14,    # y轴刻度标签字体大小
    'legend.fontsize': 14,    # 图例字体大小
})

# 皮尔逊相关系数
pearson_corr = all_data.corr(method='pearson')
plt.figure(figsize=(12, 10))
sns.heatmap(pearson_corr, annot=True, cmap='coolwarm', center=0,
            square=True, fmt='.3f', cbar_kws={'label': '皮尔逊相关系数'})
plt.title('皮尔逊相关性矩阵')
plt.tight_layout()
plt.show()

# 斯皮尔曼相关系数
spearman_corr = all_data.corr(method='spearman')
plt.figure(figsize=(12, 10))
sns.heatmap(spearman_corr, annot=True, cmap='coolwarm', center=0,
            square=True, fmt='.3f', cbar_kws={'label': '斯皮尔曼相关系数'})
plt.title('斯皮尔曼相关性矩阵')
plt.tight_layout()
plt.show()

# 高相关性特征对
high_corr_pairs = []
for i in range(len(feature_cols)):
    for j in range(i+1, len(feature_cols)):
        pearson_val = abs(pearson_corr.iloc[i, j])
        spearman_val = abs(spearman_corr.iloc[i, j])
        if pearson_val > 0.6 or spearman_val > 0.6:
            high_corr_pairs.append((feature_cols[i], feature_cols[j], pearson_val, spearman_val))

print("高相关性特征对 (|r| > 0.6):")
for pair in high_corr_pairs:
    print(f"{pair[0]} - {pair[1]}: 皮尔逊={pair[2]:.3f}, 斯皮尔曼={pair[3]:.3f}")

#cell5  特征 & 目标归一化（保证 X 的污染物列 与 y 同一套 scaler）

import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler
import joblib
import matplotlib.pyplot as plt

print("开始归一化（污染物 X/y 同一 scaler，辅助特征单独 scaler）...")

# 1) 定义列
pollution_features = ['化学需氧量(mg/l)', '氨氮(mg/l)', '总磷(mg/l)', '浊度(无)']
auxiliary_features = ['水温(℃)', 'pH值(无量纲)', '液位(无)', 'ORP(无)', '电导率(μs/cm)', '溶解氧(mg/l)']
feature_cols = pollution_features + auxiliary_features
target_scaled_cols = [f"{c}_scaled" for c in pollution_features]

# 2) 拟合 scaler（只用训练集）
scaler_y = StandardScaler()
scaler_y.fit(train_cleaned[pollution_features])     # 污染物：X/y 共用

scaler_aux = StandardScaler()
scaler_aux.fit(train_cleaned[auxiliary_features])   # 辅助特征：单独

def make_scaled(df: pd.DataFrame) -> pd.DataFrame:
    out = df.copy()

    # --- X：污染物用 scaler_y，辅助用 scaler_aux ---
    out[pollution_features] = scaler_y.transform(df[pollution_features])
    out[auxiliary_features] = scaler_aux.transform(df[auxiliary_features])

    # --- y：同样用 scaler_y，并写入 *_scaled 列 ---
    y_scaled = scaler_y.transform(df[pollution_features])
    for i, c in enumerate(pollution_features):
        out[f"{c}_scaled"] = y_scaled[:, i]

    return out

# 3) 生成 scaled 数据
train_scaled = make_scaled(train_cleaned)
val_scaled   = make_scaled(val_cleaned)
test_scaled  = make_scaled(test_cleaned)

# 4) 保存
train_scaled.to_csv('./data/train_scaled.csv', index=False, encoding='utf-8-sig')
val_scaled.to_csv('./data/val_scaled.csv',   index=False, encoding='utf-8-sig')
test_scaled.to_csv('./data/test_scaled.csv', index=False, encoding='utf-8-sig')

joblib.dump(scaler_y,   './data/scaler_y.pkl')
joblib.dump(scaler_aux, './data/scaler_aux.pkl')

print("归一化完成。")
print("train_scaled 形状:", train_scaled.shape)
print("val_scaled   形状:", val_scaled.shape)
print("test_scaled  形状:", test_scaled.shape)

# 5) 强校验：X 的污染物列 与 y 的 *_scaled 是否完全一致（应该接近 0）
diff = train_scaled[pollution_features].values - train_scaled[target_scaled_cols].values
print("\n检查：train 中 X污染物 vs y_scaled 的 max abs diff =",
      np.abs(diff).max())

# 6) 看看均值/标准差（训练集）
print("\n训练集归一化后各特征均值和标准差：")
print(pd.DataFrame({
    'mean': train_scaled[feature_cols].mean().round(3),
    'std':  train_scaled[feature_cols].std().round(3),
}))

# 7) 可视化：归一化后特征分布（训练集）
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

print("\n归一化后训练集特征分布可视化...")
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()

for i, col in enumerate(feature_cols):
    ax = axes[i]
    data = train_scaled[col].values
    ax.hist(data, bins=50, edgecolor='black', alpha=0.7)
    ax.set_title(f'{col} 归一化后分布')
    ax.axvline(0, color='red', linestyle='--', linewidth=1, alpha=0.7)
    ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.show()

#cell6 数据预处理
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
import pandas as pd
import numpy as np
from sklearn.metrics import mean_squared_error, mean_absolute_error, r2_score
import matplotlib.pyplot as plt

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

# 设置设备
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
print(f"使用设备: {device}")

# ========= 1. 读取归一化后的数据 =========
print("加载归一化后的数据...")

train_df = pd.read_csv('./data/train_scaled.csv')
val_df   = pd.read_csv('./data/val_scaled.csv')
test_df  = pd.read_csv('./data/test_scaled.csv')

# 定义特征列和目标列（和前面归一化时保持一致）
pollution_features = ['化学需氧量(mg/l)', '氨氮(mg/l)', '总磷(mg/l)', '浊度(无)']
auxiliary_features = ['水温(℃)', 'pH值(无量纲)', '液位(无)', 'ORP(无)', '电导率(μs/cm)', '溶解氧(mg/l)']
feature_cols = pollution_features + auxiliary_features

target_scaled_cols = [f"{c}_scaled" for c in pollution_features]  # 4 个标准化后的 y 列

# 构造 X 和 y（numpy 数组）
X_train = train_df[feature_cols].values
y_train = train_df[target_scaled_cols].values

X_val = val_df[feature_cols].values
y_val = val_df[target_scaled_cols].values

X_test = test_df[feature_cols].values
y_test = test_df[target_scaled_cols].values

print("数据形状:")
print(f"训练集: X={X_train.shape}, y={y_train.shape}")
print(f"验证集: X={X_val.shape},   y={y_val.shape}")
print(f"测试集: X={X_test.shape},  y={y_test.shape}")

# ========= 2. 定义滑动窗口 Dataset =========
class SeqDataset(Dataset):
    """
    x: [seq_len, num_features]
    y: [pred_steps, num_targets]
    """
    def __init__(self, X, Y, seq_len=120, pred_steps=30):
        self.X = torch.tensor(X, dtype=torch.float32)
        self.Y = torch.tensor(Y, dtype=torch.float32)
        self.seq_len = seq_len
        self.pred_steps = pred_steps

        # 能构造的样本数量：后面要预留 pred_steps 作为预测的未来
        self.max_idx = len(self.X) - seq_len - pred_steps + 1
        assert self.max_idx > 0, "数据太短，无法构造序列样本"

    def __len__(self):
        return self.max_idx

    def __getitem__(self, idx):
        # 输入序列：[idx, idx+seq_len)
        x = self.X[idx: idx + self.seq_len]                       # [seq_len, 10]
        # 目标序列：紧接在输入之后的 pred_steps 个时间步
        y = self.Y[idx + self.seq_len: idx + self.seq_len + self.pred_steps]  # [pred_steps, 4]
        return x, y

# ========= 3. 创建 Dataset 和 DataLoader =========
SEQ_LEN = 120
PRED_STEPS = 30
BATCH_SIZE = 32

# ===== val: 用 train 末尾 SEQ_LEN 行作为历史上下文 =====
X_val_ctx = np.vstack([X_train[-SEQ_LEN:], X_val])
y_val_ctx = np.vstack([y_train[-SEQ_LEN:], y_val])

# ===== test: 用 (train + val) 末尾 SEQ_LEN 行作为历史上下文 =====
X_tv = np.vstack([X_train, X_val])
y_tv = np.vstack([y_train, y_val])

X_test_ctx = np.vstack([X_tv[-SEQ_LEN:], X_test])
y_test_ctx = np.vstack([y_tv[-SEQ_LEN:], y_test])

train_dataset = SeqDataset(X_train,    y_train,    seq_len=SEQ_LEN, pred_steps=PRED_STEPS)
val_dataset   = SeqDataset(X_val_ctx,  y_val_ctx,  seq_len=SEQ_LEN, pred_steps=PRED_STEPS)
test_dataset  = SeqDataset(X_test_ctx, y_test_ctx, seq_len=SEQ_LEN, pred_steps=PRED_STEPS)

train_loader = DataLoader(train_dataset, batch_size=BATCH_SIZE, shuffle=True,  drop_last=True)
val_loader   = DataLoader(val_dataset,   batch_size=BATCH_SIZE, shuffle=False, drop_last=False)
test_loader  = DataLoader(test_dataset,  batch_size=BATCH_SIZE, shuffle=False, drop_last=False)

print("train/val/test batches:", len(train_loader), len(val_loader), len(test_loader))

print("\n数据加载器批次数:")
print(f"训练集: {len(train_loader)}")
print(f"验证集: {len(val_loader)}")
print(f"测试集: {len(test_loader)} ")

# 看一个样本（train）
sample_x, sample_y = next(iter(train_loader))
print("\n数据样本形状:")
print("输入 x:", sample_x.shape)   # [batch, seq_len, 10]
print("输出 y:", sample_y.shape)   # [batch, pred_steps, 4]

#cell7 模型定义
import torch
import torch.nn as nn

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ============================================================
# 1) Encoder：看历史 lookback
# ============================================================
class Encoder(nn.Module):
    def __init__(self, input_dim, hid_dim, num_layers=2, dropout=0.2):
        super().__init__()
        self.input_proj = nn.Linear(input_dim, hid_dim)
        self.lstm = nn.LSTM(
            input_size=hid_dim,
            hidden_size=hid_dim,
            num_layers=num_layers,
            batch_first=True,
            dropout=dropout if num_layers > 1 else 0.0
        )
        self.layer_norm = nn.LayerNorm(hid_dim)
        self.dropout = nn.Dropout(dropout)

    def forward(self, x):
        """
        x: [B, S, F]
        return:
          enc_outputs: [B, S, H]
          (h, c): [L, B, H]
        """
        x = self.input_proj(x)
        x = self.dropout(x)
        enc_outputs, (h, c) = self.lstm(x)
        enc_outputs = self.layer_norm(enc_outputs)
        return enc_outputs, (h, c)

# ============================================================
# 2) Decoder（Direct Multi-Horizon, 非自回归）
#    输入是一整段未来序列 dec_in: [B, T, dec_in_dim]
#    输出 future: [B, T, output_dim]
# ============================================================
class HorizonDecoderRNN(nn.Module):
    def __init__(self, dec_in_dim, hid_dim, output_dim, num_layers=2, dropout=0.2, use_attention=True):
        super().__init__()
        self.use_attention = use_attention

        self.input_proj = nn.Linear(dec_in_dim, hid_dim)
        self.lstm = nn.LSTM(
            input_size=hid_dim,
            hidden_size=hid_dim,
            num_layers=num_layers,
            batch_first=True,
            dropout=dropout if num_layers > 1 else 0.0
        )
        self.layer_norm = nn.LayerNorm(hid_dim)
        self.dropout = nn.Dropout(dropout)

        # 输出 head：如果用 attention，就拼 [dec_h, context] => 2H
        head_in = hid_dim * 2 if use_attention else hid_dim
        self.head = nn.Sequential(
            nn.Linear(head_in, hid_dim),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hid_dim, output_dim)
        )

    def forward(self, dec_in, init_state, enc_outputs=None):
        """
        dec_in: [B, T, dec_in_dim]
        init_state: (h, c) from encoder, each [L,B,H]
        enc_outputs: [B, S, H] (用于 attention，可为 None)
        return:
          out: [B, T, output_dim]
        """
        x = self.input_proj(dec_in)
        x = self.dropout(x)

        dec_h, _ = self.lstm(x, init_state)     # [B, T, H]
        dec_h = self.layer_norm(dec_h)

        if (not self.use_attention) or (enc_outputs is None):
            out = self.head(dec_h)              # [B, T, O]
            return out

        # ===== 全局 dot-product attention（向量化，一次算完所有 T）=====
        # queries=dec_h: [B,T,H], keys=enc_outputs: [B,S,H]
        scores = torch.bmm(dec_h, enc_outputs.transpose(1, 2))      # [B,T,S]
        weights = torch.softmax(scores, dim=-1)                      # [B,T,S]
        context = torch.bmm(weights, enc_outputs)                    # [B,T,H]

        out_in = torch.cat([dec_h, context], dim=-1)                 # [B,T,2H]
        out = self.head(out_in)                                      # [B,T,O]
        return out

# ============================================================
# 3) Direct Multi-Horizon 模型（Encoder + Decoder）
#    关键：decoder 不吃 prev_y，不滚动；一次性输出未来 T 步
# ============================================================
class DirectMultiHorizon_ED(nn.Module):
    def __init__(
        self,
        input_dim=10,        # 10 = 4污染物 + 6辅助
        output_dim=4,
        time_feature_dim=0,  # 未来每一步的时间特征维度（没有就设 0）
        hid_dim=64,
        num_layers=2,
        pred_steps=30,
        dropout=0.2,
        use_src_time=False,      # 是否把历史时间特征拼进 encoder
        use_future_time=False,   # 是否把未来时间特征拼进 decoder 输入序列
        use_aux_last=True,       # 是否把 aux_last(6维)拼进 decoder 输入
        residual_to_last_y=True, # 是否 pred = last_y + delta（强烈建议开）
        use_attention=True,      # decoder 对 encoder outputs 做 attention
    ):
        super().__init__()
        self.pred_steps = pred_steps
        self.output_dim = output_dim
        self.time_feature_dim = time_feature_dim
        self.aux_dim = input_dim - output_dim

        self.use_src_time = use_src_time
        self.use_future_time = use_future_time
        self.use_aux_last = use_aux_last
        self.residual_to_last_y = residual_to_last_y

        enc_in_dim = input_dim + (time_feature_dim if use_src_time else 0)
        self.encoder = Encoder(enc_in_dim, hid_dim, num_layers=num_layers, dropout=dropout)

        # decoder 每步输入：last_y(4) + aux_last(6可选) + future_time(可选)
        dec_in_dim = output_dim
        if use_aux_last:
            dec_in_dim += self.aux_dim
        if use_future_time:
            dec_in_dim += time_feature_dim

        self.decoder = HorizonDecoderRNN(
            dec_in_dim=dec_in_dim,
            hid_dim=hid_dim,
            output_dim=output_dim,    # 输出 delta 或 y
            num_layers=num_layers,
            dropout=dropout,
            use_attention=use_attention
        )

    def forward(self, src, target=None, src_time_features=None, future_time_features=None):
        """
        src: [B, S, 10]
        target: 兼容你原来的 train_loop（这里不需要，用不上也不报错）
        src_time_features: [B, S, time_dim] 可选
        future_time_features: [B, T, time_dim] 可选
        return: [B, T, 4]
        """
        B, S, F = src.shape
        T = self.pred_steps

        # ---- encoder 输入 ----
        if self.use_src_time:
            assert src_time_features is not None, "use_src_time=True 但未提供 src_time_features"
            enc_in = torch.cat([src, src_time_features], dim=-1)     # [B,S, F+time]
        else:
            enc_in = src

        enc_outputs, (h, c) = self.encoder(enc_in)                   # enc_outputs [B,S,H]

        # ---- last_y / aux_last ----
        last_y = src[:, -1, :self.output_dim]                         # [B,4]
        last_y_T = last_y.unsqueeze(1).expand(B, T, self.output_dim)  # [B,T,4]

        parts = [last_y_T]

        if self.use_aux_last:
            aux_last = src[:, -1, self.output_dim:]                   # [B,6]
            aux_T = aux_last.unsqueeze(1).expand(B, T, aux_last.size(-1))  # [B,T,6]
            parts.append(aux_T)

        if self.use_future_time:
            assert future_time_features is not None, "use_future_time=True 但未提供 future_time_features"
            assert future_time_features.shape[1] == T, "future_time_features 的长度必须等于 pred_steps"
            parts.append(future_time_features)                        # [B,T,time_dim]

        dec_in = torch.cat(parts, dim=-1)                             # [B,T,dec_in_dim]

        # ---- decoder 一次性输出未来 T 步 ----
        delta_or_y = self.decoder(dec_in, init_state=(h, c), enc_outputs=enc_outputs)  # [B,T,4]

        if self.residual_to_last_y:
            pred = last_y_T + delta_or_y
        else:
            pred = delta_or_y

        return pred

    @torch.no_grad()
    def predict(self, src, src_time_features=None, future_time_features=None):
        self.eval()
        return self.forward(src, target=None, src_time_features=src_time_features, future_time_features=future_time_features)

# ============================================================
# 4) 权重初始化（你原来的也能用）
# ============================================================
def initialize_weights(model):
    for name, param in model.named_parameters():
        if 'weight' in name and param.dim() >= 2:
            if 'lstm' in name:
                nn.init.xavier_uniform_(param)
            else:
                nn.init.kaiming_normal_(param, mode='fan_out', nonlinearity='relu')
        elif 'bias' in name:
            nn.init.constant_(param, 0.0)

# ============================================================
# 5) 实例化（注意：没有 local_window 了！）
# ============================================================
sample_x, sample_y = next(iter(train_loader))
SEQ_LEN    = sample_x.shape[1]
INPUT_DIM  = sample_x.shape[2]  # 10
PRED_STEPS = sample_y.shape[1]  # 例如 60
OUTPUT_DIM = sample_y.shape[2]  # 4

HID_DIM = 64
NUM_LAYERS = 2
DROPOUT = 0.2

model = DirectMultiHorizon_ED(
    input_dim=INPUT_DIM,
    output_dim=OUTPUT_DIM,
    time_feature_dim=0,      # 你还没接时间特征就先设 0
    hid_dim=HID_DIM,
    num_layers=NUM_LAYERS,
    pred_steps=PRED_STEPS,
    dropout=DROPOUT,
    use_src_time=False,
    use_future_time=False,
    use_aux_last=True,
    residual_to_last_y=True,
    use_attention=True,      # 要更“像你原 seq2seq”就开着
).to(device)

initialize_weights(model)

print("\n=== DirectMultiHorizon_ED (Encoder+Decoder, non-autoregressive) ===")
print(f"INPUT_DIM={INPUT_DIM}, OUTPUT_DIM={OUTPUT_DIM}, SEQ_LEN={SEQ_LEN}, PRED_STEPS={PRED_STEPS}")
print(f"参数总量: {sum(p.numel() for p in model.parameters()):,}")

# ============================================================
# 6) 形状检查（兼容你原来的 target 参数）
# ============================================================
test_x, test_y = next(iter(train_loader))
test_x = test_x.to(device)
test_y = test_y.to(device)

with torch.no_grad():
    out = model(test_x, target=test_y)   # target 传了也没关系（这里只是兼容）
print("\n=== 形状检查 ===")
print("输入 x:", test_x.shape)
print("目标 y:", test_y.shape)
print("输出  :", out.shape)  # [B, PRED_STEPS, OUTPUT_DIM]

#cell8 训练
import torch
import torch.nn as nn
import torch.optim as optim
import matplotlib.pyplot as plt


def train_model(
        model,
        train_loader,
        val_loader,
        num_epochs=50,
        learning_rate=1e-3,
        save_path='./data/best_model.pth',
):
    # 损失函数和优化器
    criterion = nn.MSELoss()
    optimizer = optim.AdamW(model.parameters(), lr=learning_rate, weight_decay=1e-5)
    scheduler = optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, mode='min', factor=0.5, patience=5, verbose=True
    )

    train_losses = []
    val_losses = []
    best_val_loss = float('inf')
    patience = 10
    no_improve = 0

    print("开始训练...")

    for epoch in range(num_epochs):
        # ========= 训练 =========
        model.train()
        train_loss_sum = 0.0
        train_batches = 0
        model.teacher_forcing_ratio = max(0.1, 1.0 - epoch / (num_epochs * 0.8))

        for batch_idx, (x, y) in enumerate(train_loader):
            x = x.to(device)  # [B, T, 10]
            y = y.to(device)  # [B, PRED_STEPS, 4]

            optimizer.zero_grad()

            # 训练时使用 teacher forcing
            output = model(x, target=y)  # [B, PRED_STEPS, 4]

            loss = criterion(output, y)
            loss.backward()

            # 梯度裁剪，防止梯度爆炸
            torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
            optimizer.step()

            train_loss_sum += loss.item()
            train_batches += 1

            if batch_idx % 50 == 0:
                print(f'Epoch {epoch + 1}, Batch {batch_idx}, Loss: {loss.item():.6f}')

        avg_train_loss = train_loss_sum / train_batches
        train_losses.append(avg_train_loss)

        # ========= 验证 =========
        model.eval()
        val_loss_sum = 0.0
        val_batches = 0

        with torch.no_grad():
            for x, y in val_loader:
                x = x.to(device)
                y = y.to(device)

                # 验证时不使用 teacher forcing（纯自回归）
                output = model(x)  # [B, PRED_STEPS, 4]
                loss = criterion(output, y)

                val_loss_sum += loss.item()
                val_batches += 1

        avg_val_loss = val_loss_sum / val_batches
        val_losses.append(avg_val_loss)

        print(f'Epoch {epoch + 1}/{num_epochs}:')
        print(f'  训练损失: {avg_train_loss:.6f}')
        print(f'  验证损失: {avg_val_loss:.6f}')

        # 学习率调度（看验证损失）
        scheduler.step(avg_val_loss)

        # 早停 & 保存最优模型
        if avg_val_loss < best_val_loss:
            best_val_loss = avg_val_loss
            torch.save(model.state_dict(), save_path)
            no_improve = 0
            print(f'  保存最佳模型，验证损失: {best_val_loss:.6f}')
        else:
            no_improve += 1
            if no_improve >= patience:
                print(f'早停：验证损失在 {patience} 个 epoch 内没有改善')
                break

        print('-' * 60)

    # ========= 画训练曲线 =========
    plt.figure(figsize=(8, 5))
    plt.plot(train_losses, label='训练损失', alpha=0.8)
    plt.plot(val_losses, label='验证损失', alpha=0.8)
    plt.xlabel('Epoch')
    plt.ylabel('MSE Loss')
    plt.title('训练 / 验证损失曲线')
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.show()

    return train_losses, val_losses


# 调用训练
train_losses, val_losses = train_model(
    model,
    train_loader,
    val_loader,
    num_epochs=100,
    learning_rate=1e-3,
    save_path='./data/best_model.pth'
)

# 训练结束后加载最佳模型
model.load_state_dict(torch.load('./data/best_model.pth', map_location=device))
model.to(device)
print("已加载最佳模型权重")
print("训练完成！")


#cell9 数据可视化
import os
import json
import joblib
import numpy as np
import matplotlib.pyplot as plt
from sklearn.metrics import mean_squared_error, mean_absolute_error, r2_score
from matplotlib.ticker import FormatStrFormatter, MultipleLocator
import torch

# 字体（如果 Ubuntu 没有 YaHei，可改成 Noto Sans CJK SC / SimHei）
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei', 'Noto Sans CJK SC', 'SimHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False


# =========================================================
# 0. 基础工具
# =========================================================
def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)

def inverse_denorm(preds_std, tgts_std, scaler_y):
    N, T, O = preds_std.shape
    pred_2d = preds_std.reshape(-1, O)
    tgt_2d  = tgts_std.reshape(-1, O)
    preds_phy = scaler_y.inverse_transform(pred_2d).reshape(N, T, O)
    tgts_phy  = scaler_y.inverse_transform(tgt_2d).reshape(N, T, O)
    return preds_phy, tgts_phy

def calculate_metrics_1d(y_true, y_pred, mape_eps=1e-12):
    mse = mean_squared_error(y_true, y_pred)
    mae = mean_absolute_error(y_true, y_pred)
    rmse = np.sqrt(mse)
    try:
        r2 = r2_score(y_true, y_pred)
        if np.isnan(r2) or np.isinf(r2):
            r2 = -999
    except Exception:
        r2 = -999

    denom = np.maximum(np.abs(y_true), mape_eps)
    mape = np.mean(np.abs((y_true - y_pred) / denom)) * 100
    return {"MSE": mse, "MAE": mae, "RMSE": rmse, "R2": r2, "MAPE": mape}

def compute_metrics(preds, tgts, feature_names, pred_steps):
    results = {}
    for step in range(pred_steps):
        step_dict = {}
        for i, feat in enumerate(feature_names):
            y_true = tgts[:, step, i]
            y_pred = preds[:, step, i]
            step_dict[feat] = calculate_metrics_1d(y_true, y_pred)
        results[f"step_{step+1}"] = step_dict
    return results

def to_serializable(obj):
    if isinstance(obj, (np.floating,)):
        return float(obj)
    if isinstance(obj, (np.integer,)):
        return int(obj)
    if isinstance(obj, dict):
        return {k: to_serializable(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [to_serializable(v) for v in obj]
    return obj

def normalize_steps_to_plot(steps_to_plot, pred_steps):
    steps = sorted(set(int(s) for s in steps_to_plot))
    steps = [s for s in steps if 1 <= s <= pred_steps]
    if len(steps) == 0:
        raise ValueError(f"steps_to_plot 全被过滤掉了，请确保在 [1, {pred_steps}] 内")
    return steps

def print_selected_r2(metrics_results, pollution_features, steps_to_plot, title=""):
    if title:
        print("\n" + "="*70)
        print(title)
        print("="*70)
    for s in steps_to_plot:
        key = f"step_{s}"
        print(f"\nstep_{s}:")
        for feat in pollution_features:
            r2 = metrics_results[key][feat]["R2"]
            rmse = metrics_results[key][feat]["RMSE"]
            mae = metrics_results[key][feat]["MAE"]
            print(f"  {feat}: R2={r2:.6f}, RMSE={rmse:.6f}, MAE={mae:.6f}")

# =========================================================
# 1. 全局曲线：指标随步长变化（保留）
# =========================================================
def plot_metric_over_steps(metrics_results, pollution_features, pred_steps, metric='R2', title_prefix=''):
    xs = np.arange(1, pred_steps + 1)
    plt.figure(figsize=(9, 4.5))
    for feat in pollution_features:
        ys = []
        for s in xs:
            val = metrics_results[f"step_{s}"][feat][metric]
            if val == -999 or np.isnan(val) or np.isinf(val):
                val = np.nan
            ys.append(val)
        plt.plot(xs, ys, marker='o', label=feat.split('(')[0])
    plt.xlabel('预测步长(step)')
    plt.ylabel(metric)
    plt.title(f'{title_prefix}{metric} vs step')
    plt.grid(True, alpha=0.3)
    plt.legend()
    plt.tight_layout()
    plt.show()

# =========================================================
# 2. 指定步长：时序/散点/误差分布（保持你原风格）
# =========================================================
def plot_prediction_vs_actual_selected(predictions, targets, pollution_features,
                                       steps_to_plot, num_samples=9000):
    rows = len(steps_to_plot)
    cols = len(pollution_features)
    fig, axes = plt.subplots(rows, cols, figsize=(5*cols, 3.8*rows))
    if rows == 1:
        axes = axes.reshape(1, -1)

    sample_indices = np.arange(min(num_samples, predictions.shape[0]))

    for r, s in enumerate(steps_to_plot):
        step_idx = s - 1
        for c, feature in enumerate(pollution_features):
            ax = axes[r, c]
            y_true = targets[sample_indices, step_idx, c]
            y_pred = predictions[sample_indices, step_idx, c]
            ax.plot(sample_indices, y_true, 'b-', label='真实值', alpha=0.7, linewidth=1)
            ax.plot(sample_indices, y_pred, 'r-', label='预测值', alpha=0.7, linewidth=1)
            ax.set_title(f'{feature.split("(")[0]} - 步长{s}')
            ax.set_xlabel('样本')
            ax.set_ylabel('原始数值')
            if r == 0 and c == cols - 1:
                ax.legend()
            ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.show()

def plot_scatter_pred_vs_actual_selected(predictions, targets, pollution_features, steps_to_plot):
    rows = len(steps_to_plot)
    cols = len(pollution_features)
    fig, axes = plt.subplots(rows, cols, figsize=(5*cols, 3.8*rows))
    if rows == 1:
        axes = axes.reshape(1, -1)

    for r, s in enumerate(steps_to_plot):
        step_idx = s - 1
        for c, feature in enumerate(pollution_features):
            ax = axes[r, c]
            y_true = targets[:, step_idx, c]
            y_pred = predictions[:, step_idx, c]

            ax.scatter(y_true, y_pred, alpha=0.3, s=5)

            min_val = min(y_true.min(), y_pred.min())
            max_val = max(y_true.max(), y_pred.max())
            ax.plot([min_val, max_val], [min_val, max_val], 'r--', linewidth=1)

            corr = np.corrcoef(y_true, y_pred)[0, 1]
            r2 = r2_score(y_true, y_pred)

            ax.set_xlabel('真实值')
            ax.set_ylabel('预测值')
            ax.set_title(f'{feature.split("(")[0]} - 步长{s}')
            ax.grid(True, alpha=0.3)
            ax.text(0.05, 0.95,
                    f'Corr = {corr:.3f}\nR² = {r2:.3f}',
                    transform=ax.transAxes, va='top',
                    bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8),
                    fontsize=9)

    plt.tight_layout()
    plt.show()

def plot_error_distribution_selected(predictions, targets, pollution_features, steps_to_plot):
    rows = len(steps_to_plot)
    cols = len(pollution_features)
    fig, axes = plt.subplots(rows, cols, figsize=(5*cols, 3.8*rows))
    if rows == 1:
        axes = axes.reshape(1, -1)

    for r, s in enumerate(steps_to_plot):
        step_idx = s - 1
        for c, feature in enumerate(pollution_features):
            ax = axes[r, c]
            y_true = targets[:, step_idx, c]
            y_pred = predictions[:, step_idx, c]
            errors = y_pred - y_true

            ax.hist(errors, bins=50, alpha=0.7, edgecolor='black')
            ax.set_title(f'{feature.split("(")[0]} - 步长{s} 误差分布')
            ax.set_xlabel('预测误差')
            ax.set_ylabel('频数')
            ax.grid(True, alpha=0.3)

            mean_error = np.mean(errors)
            ax.axvline(mean_error, color='red', linestyle='--', alpha=0.8, label=f'均值: {mean_error:.4f}')
            ax.axvline(0, color='green', linestyle='-', alpha=0.8, label='零误差')
            if r == 0 and c == cols - 1:
                ax.legend()

    plt.tight_layout()
    plt.show()

# =========================================================
# 2.5 新增：随机抽 k=5 个窗口，画“未来30步整段曲线”对比（你要的）
# =========================================================
def _nice_tick_step(span, prefer_base=1e-1):
    """
    span: y轴范围
    prefer_base: 你希望的量化级别（1e-1 或 1e-2）
    返回一个“好看”的步长：0.1/0.05/0.02/0.01/...
    """
    # 候选从粗到细（你说 e-1 不行再 e-2）
    candidates = [1e-1, 5e-2, 2e-2, 1e-2, 5e-3, 2e-3, 1e-3]
    # 先从 prefer_base 开始往后找
    start = candidates.index(prefer_base) if prefer_base in candidates else 0
    for step in candidates[start:]:
        # 目标大概 4~6 个主刻度
        n_ticks = span / step if step > 0 else 999
        if 3 <= n_ticks <= 7:
            return step
    # 实在不行：span 很小，就取 span/4 的“近似好看步长”
    step = max(span / 4.0, candidates[-1])
    return step

def plot_k_cases_horizon_pretty(preds, tgts, feature_names, k=5, seed=42,
                                prefer_base=1e-1, title="随机抽取 K 个窗口：未来 T 步预测 vs 真实（物理空间）"):
    """
    preds/tgts: [N, T, O]（物理空间）
    - 每行一个 case
    - 每列一个污染物
    - 只保留最左列 y 轴刻度（“量化条/标尺”），其余列去掉 y tick label
    - 子图贴近
    - y 轴主刻度按 prefer_base（1e-1 或 1e-2）量化
    """
    N, T, O = preds.shape
    assert tgts.shape == preds.shape
    assert O == len(feature_names)

    rng = np.random.default_rng(seed)
    case_indices = rng.choice(N, size=min(k, N), replace=(N < k)).astype(int)
    k = len(case_indices)
    xs = np.arange(1, T + 1)

    # 更紧凑：缩小整体尺寸
    fig, axes = plt.subplots(k, O, figsize=(4.2*O, 2.2*k), sharex=True)
    if k == 1:
        axes = axes.reshape(1, -1)

    # 子图间距更贴近
    fig.subplots_adjust(left=0.06, right=0.995, top=0.92, bottom=0.07,
                        wspace=0.18, hspace=0.18)

    for r, idx in enumerate(case_indices):
        for c in range(O):
            ax = axes[r, c]
            y_true = tgts[idx, :, c]
            y_pred = preds[idx, :, c]

            # 线条更干净：去 marker（或者你想保留就加回去）
            ax.plot(xs, y_true, "b-", linewidth=1.8, label="真实值")
            ax.plot(xs, y_pred, "r-", linewidth=1.8, label="预测值")

            # y轴范围：紧一点 + 留一点边距
            ymin = float(min(y_true.min(), y_pred.min()))
            ymax = float(max(y_true.max(), y_pred.max()))
            span = max(ymax - ymin, 1e-12)
            step = _nice_tick_step(span, prefer_base=prefer_base)
            margin = max(0.5 * step, 0.08 * span)
            ax.set_ylim(ymin - margin, ymax + margin)

            # y轴“量化条”：主刻度固定步长 + 小数位
            ax.yaxis.set_major_locator(MultipleLocator(step))
            decimals = 1 if step >= 1e-1 else 2  # e-1 就 1 位小数，e-2 就 2 位
            ax.yaxis.set_major_formatter(FormatStrFormatter(f"%.{decimals}f"))

            # 只保留最左列的 y 刻度（你说“左边要有量化条”）
            if c != 0:
                ax.tick_params(axis="y", labelleft=False)

            # 标题/标签
            if r == 0:
                ax.set_title(feature_names[c].split("(")[0])
            if c == 0:
                ax.set_ylabel(f"case#{idx}\n数值")
            if r == k - 1:
                ax.set_xlabel("预测步长(step)")

            ax.grid(True, alpha=0.25)

            # 只放一次 legend
            if r == 0 and c == O - 1:
                ax.legend()

    fig.suptitle(title, fontsize=13)
    plt.show()

    return case_indices.tolist()

# =========================================================
# 3. 用 test_loader 跑出 test_predictions/test_targets（标准化空间）
# =========================================================
@torch.no_grad()
def run_inference_on_loader(model, loader, device):
    model.eval()
    preds_list, tgts_list = [], []
    for x, y in loader:
        x = x.to(device)
        y = y.to(device)
        pred = model(x)  # DirectMultiHorizon_ED: [B, pred_steps, 4]
        preds_list.append(pred.detach().cpu().numpy())
        tgts_list.append(y.detach().cpu().numpy())
    preds = np.concatenate(preds_list, axis=0)
    tgts  = np.concatenate(tgts_list, axis=0)
    return preds, tgts

# =========================================================
# 4. 一键报告：反标准化 + 指标 + 保存 + 画图（指定步长 + 随机k窗口）
# =========================================================
def run_full_report(
    name,
    preds_std,
    tgts_std,
    scaler_y,
    pollution_features,
    out_dir='./data',
    num_samples=9000,
    steps_to_plot=(1, 2, 5, 10, 30),
    draw_selected_plots=True,
    draw_global_plots=True,
    draw_random_cases=True,
    k_cases=5,
    seed_cases=42,
):
    ensure_dir(out_dir)

    assert preds_std.shape == tgts_std.shape
    N, pred_steps, O = preds_std.shape
    assert O == len(pollution_features)

    steps_to_plot = normalize_steps_to_plot(steps_to_plot, pred_steps)

    print("\n" + "="*80)
    print(f"[{name}] 反标准化 & 评估")
    print("="*80)
    print(f"样本数 N={N}, PRED_STEPS={pred_steps}, 输出维度 O={O}")
    print(f"可视化步长 steps_to_plot = {steps_to_plot}")

    preds_phy, tgts_phy = inverse_denorm(preds_std, tgts_std, scaler_y)

    np.save(os.path.join(out_dir, f'{name}_preds_denorm.npy'), preds_phy)
    np.save(os.path.join(out_dir, f'{name}_tgts_denorm.npy'), tgts_phy)

    metrics_phy = compute_metrics(preds_phy, tgts_phy, pollution_features, pred_steps)
    metrics_std = compute_metrics(preds_std, tgts_std, pollution_features, pred_steps)

    print_selected_r2(metrics_phy, pollution_features, steps_to_plot, title=f"{name}（物理空间：选定步长）")
    print_selected_r2(metrics_std, pollution_features, steps_to_plot, title=f"{name}（标准化空间：选定步长）")

    save_json = {
        "name": name,
        "pred_steps": pred_steps,
        "steps_to_plot": list(steps_to_plot),
        "metrics_physical": metrics_phy,
        "metrics_standardized": metrics_std,
    }
    with open(os.path.join(out_dir, f'{name}_model_performance.json'), 'w', encoding='utf-8') as f:
        json.dump(to_serializable(save_json), f, ensure_ascii=False, indent=2)
    print(f"\n已保存: {os.path.join(out_dir, f'{name}_model_performance.json')}")

    # ===== 你原来的“指定步长”图 =====
    if draw_selected_plots:
        print("\n生成【选定步长】可视化（物理空间）...")
        plot_prediction_vs_actual_selected(preds_phy, tgts_phy, pollution_features, steps_to_plot, num_samples=num_samples)
        plot_scatter_pred_vs_actual_selected(preds_phy, tgts_phy, pollution_features, steps_to_plot)
        plot_error_distribution_selected(preds_phy, tgts_phy, pollution_features, steps_to_plot)

    # ===== 你要的：随机 k=5 个窗口，画“未来30步整段曲线” =====
    if draw_random_cases:
        print(f"\n生成【随机{k_cases}个窗口】未来{pred_steps}步整段曲线对比（物理空间）...")
        picked = plot_k_cases_horizon(preds_phy, tgts_phy, pollution_features, k=k_cases, seed=seed_cases, title_prefix="case")
        print("本次抽到的样本 idx:", picked)

    # ===== 全局曲线（保留），热力图已删除 =====
    if draw_global_plots:
        print("\n生成【全局视角】曲线（物理空间）...")
        plot_metric_over_steps(metrics_phy, pollution_features, pred_steps, metric='R2',  title_prefix=f'{name} ')
        plot_metric_over_steps(metrics_phy, pollution_features, pred_steps, metric='RMSE', title_prefix=f'{name} ')

    return metrics_phy, metrics_std


# =========================================================
# 5. 调用：直接用 test_loader 生成 npy，再跑报告
# =========================================================
if __name__ == "__main__":
    out_dir = "./data"
    ensure_dir(out_dir)

    pollution_features = ['化学需氧量(mg/l)', '氨氮(mg/l)', '总磷(mg/l)', '浊度(无)']
    steps_to_plot = (1, 2, 5, 10, 30)

    # 1) 推理得到标准化空间预测/真值
    test_predictions, test_targets = run_inference_on_loader(model, test_loader, device)

    # 2) 保存
    np.save(os.path.join(out_dir, "test_predictions.npy"), test_predictions)
    np.save(os.path.join(out_dir, "test_targets.npy"), test_targets)
    print("已保存:", os.path.join(out_dir, "test_predictions.npy"))
    print("已保存:", os.path.join(out_dir, "test_targets.npy"))

    # 3) 反标准化 & 可视化
    scaler_y = joblib.load(os.path.join(out_dir, "scaler_y.pkl"))

    run_full_report(
        name="direct_mh",
        preds_std=test_predictions,
        tgts_std=test_targets,
        scaler_y=scaler_y,
        pollution_features=pollution_features,
        out_dir=out_dir,
        num_samples=9000,
        steps_to_plot=steps_to_plot,
        draw_selected_plots=True,
        draw_global_plots=True,
        draw_random_cases=True,
        k_cases=5,
        seed_cases=42,
    )
