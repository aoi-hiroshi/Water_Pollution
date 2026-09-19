import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from scipy import stats
from statsmodels.tsa.seasonal import STL
from statsmodels.tsa.stattools import acf, pacf
import shap
from sklearn.preprocessing import StandardScaler
from sklearn.ensemble import RandomForestClassifier
import warnings
warnings.filterwarnings('ignore')

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

# cell1 读取数据
train_data = pd.read_csv('./data/train_data.csv')
test_data = pd.read_csv('./data/test_data.csv')

print("训练集形状:", train_data.shape)
print("测试集形状:", test_data.shape)
print("\n数据列名:")
print(train_data.columns.tolist())


company1_data = train_data[train_data['label'] == 1]
print(f"\n公司1数据量: {len(company1_data)}")
print("\n公司1十个特征的统计信息:")
feature_cols = ['水温(℃)', 'pH值(无量纲)', '化学需氧量(mg/l)', '氨氮(mg/l)',
                '总磷(mg/l)', '液位(无)', 'ORP(无)', '电导率(μs/cm)',
                '溶解氧(mg/l)', '浊度(无)']
print(company1_data[feature_cols].describe())


fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
for i, col in enumerate(feature_cols):
    axes[i].plot(company1_data[col].values[:700])
    axes[i].set_title(f'公司1-{col}')
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

# cell2  数据清洗
def sliding_zscore_outlier_detection(data, window=50, threshold=3):
    outliers = np.zeros(len(data), dtype=bool)
    for i in range(window, len(data)):
        window_data = data[i - window:i]
        z_score = np.abs((data[i] - np.mean(window_data)) / np.std(window_data))
        if z_score > threshold:
            outliers[i] = True
    return outliers


def kalman_filter_fill(data, outliers):
    filled_data = data.copy()
    Q = 1e-5  # 过程噪声
    R = 0.1  # 观测噪声

    for i in range(1, len(data)):
        if outliers[i]:
            # 卡尔曼预测
            x_pred = filled_data[i - 1]
            P_pred = Q
            K = P_pred / (P_pred + R)
            if i > 1:
                filled_data[i] = x_pred + K * (filled_data[i - 1] - x_pred)
            else:
                filled_data[i] = filled_data[i - 1]
    return filled_data


print("数据空值检查:")
print("训练集空值:", train_data.isnull().sum().sum())
print("测试集空值:", test_data.isnull().sum().sum())

# 异常值检测和修正
train_cleaned = train_data.copy()
test_cleaned = test_data.copy()

outlier_stats = {}
for col in feature_cols:
    # 训练集异常值处理
    outliers_train = sliding_zscore_outlier_detection(train_data[col].values)
    train_cleaned[col] = kalman_filter_fill(train_data[col].values, outliers_train)

    # 测试集异常值处理
    outliers_test = sliding_zscore_outlier_detection(test_data[col].values)
    test_cleaned[col] = kalman_filter_fill(test_data[col].values, outliers_test)

    outlier_stats[col] = {
        'train_outliers': outliers_train.sum(),
        'test_outliers': outliers_test.sum()
    }

print("\n异常值统计:")
for col, stats in outlier_stats.items():
    print(f"{col}: 训练集{stats['train_outliers']}个, 测试集{stats['test_outliers']}个")

# 清洗后数据可视化对比

fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
plt.rcParams.update({
    'font.size': 20,
    'axes.titlesize': 20,
    'axes.labelsize': 20,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 14,
})
for i, col in enumerate(feature_cols):
    axes[i].plot(train_data[col].values[:700], alpha=0.7, label='原始数据')
    axes[i].plot(train_cleaned[col].values[:700], alpha=0.8, label='清洗后')
    axes[i].set_title(f'{col}清洗对比')
    axes[i].legend()
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()
plt.savefig('cleaned.png', dpi=300, bbox_inches='tight')
# 保存清洗后的数据
train_cleaned.to_csv('./data/train_cleaned.csv', index=False)
test_cleaned.to_csv('./data/test_cleaned.csv', index=False)
print("清洗后数据已保存")

# cell3 分位点统计
subset = train_cleaned[train_cleaned['label'] == 1]

# ——— 直方图 ———
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()
plt.rcParams.update({
    'font.size': 18,
    'axes.titlesize': 18,
    'axes.labelsize': 18,
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 14,
})
for i, col in enumerate(feature_cols):
    axes[i].hist(subset[col], bins=50, alpha=0.7, edgecolor='black')
    axes[i].set_title(f'{col} 分布')
    axes[i].grid(True, alpha=0.3)

plt.tight_layout()
plt.show()

# ——— 箱形图 ———
fig, axes = plt.subplots(2, 5, figsize=(20, 8))
axes = axes.flatten()

for i, col in enumerate(feature_cols):
    axes[i].boxplot(subset[col], vert=True)
    axes[i].set_title(f'{col} 箱形图 (label=1)')
    axes[i].grid(True, alpha=0.3)

plt.tight_layout()
plt.show()

# ——— 分位点统计 ———
print("各特征 (label=1) 分位点统计:")
quantiles = [0.1, 0.25, 0.5, 0.75, 0.9]

for col in feature_cols:
    q_values = np.quantile(subset[col], quantiles)
    q_str = " | ".join([f"Q{int(q*100)}:{v:.2f}" for q, v in zip(quantiles, q_values)])
    print(f"{col}: {q_str}")

# 按公司分组的分布对比
fig, axes = plt.subplots(2, 5, figsize=(20, 8))

axes = axes.flatten()
for i, col in enumerate(feature_cols):
    for company in range(1, 7):
        company_data = train_cleaned[train_cleaned['label'] == company][col]
        axes[i].hist(company_data, alpha=0.5, label=f'公司{company}', bins=20)
    clean_title = col.replace('(label=1)', '').strip()
    axes[i].set_title(f'{col}各公司分布')
    axes[i].legend()
    axes[i].grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

#  cell4 数据分析
# 皮尔逊相关系数
pearson_corr = train_cleaned[feature_cols].corr(method='pearson')

plt.figure(figsize=(12, 10))
plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 18,
    'axes.labelsize': 14,
    'xtick.labelsize': 15,
    'ytick.labelsize': 15,
    'legend.fontsize': 14,
})
sns.heatmap(pearson_corr, annot=True, cmap='coolwarm', center=0,
            square=True, fmt='.3f', cbar_kws={'label': '皮尔逊相关系数'})
plt.tight_layout()
plt.show()

# 斯皮尔曼相关系数
spearman_corr = train_cleaned[feature_cols].corr(method='spearman')
plt.figure(figsize=(12, 10))
sns.heatmap(spearman_corr, annot=True, cmap='coolwarm', center=0,
            square=True, fmt='.3f', cbar_kws={'label': '斯皮尔曼相关系数'})
plt.title('斯皮尔曼相关性矩阵')
plt.tight_layout()
plt.show()

# 相关性强度分析
high_corr_pairs = []
for i in range(len(feature_cols)):
    for j in range(i+1, len(feature_cols)):
        pearson_val = abs(pearson_corr.iloc[i, j])
        spearman_val = abs(spearman_corr.iloc[i, j])
        if pearson_val > 0.7 or spearman_val > 0.7:
            high_corr_pairs.append((feature_cols[i], feature_cols[j], pearson_val, spearman_val))

print("高相关性特征对 (|r| > 0.7):")
for pair in high_corr_pairs:
    print(f"{pair[0]} - {pair[1]}: 皮尔逊={pair[2]:.3f}, 斯皮尔曼={pair[3]:.3f}")

# cell5 逻辑回归
import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import accuracy_score, precision_score, recall_score, f1_score, confusion_matrix, classification_report
import matplotlib.pyplot as plt
import seaborn as sns
import joblib

# 设置中文字体
plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

# 读取清洗后的数据
train_data = pd.read_csv('./data/train_cleaned.csv')
test_data = pd.read_csv('./data/test_cleaned.csv')

feature_cols = ['水温(℃)', 'pH值(无量纲)', '化学需氧量(mg/l)', '氨氮(mg/l)',
                '总磷(mg/l)', '液位(无)', 'ORP(无)', '电导率(μs/cm)',
                '溶解氧(mg/l)', '浊度(无)']

# 准备数据
X_train = train_data[feature_cols]
y_train = train_data['label']
X_test = test_data[feature_cols]
y_test = test_data['label']

# 数据标准化
scaler = StandardScaler()
X_train_scaled = scaler.fit_transform(X_train)
X_test_scaled = scaler.transform(X_test)

# 训练逻辑回归模型
lr_model = LogisticRegression(max_iter=1000, random_state=42)
lr_model.fit(X_train_scaled, y_train)

# 预测
y_pred_lr = lr_model.predict(X_test_scaled)
y_proba_lr = lr_model.predict_proba(X_test_scaled)

# 评估指标
lr_accuracy = accuracy_score(y_test, y_pred_lr)
lr_precision = precision_score(y_test, y_pred_lr, average='macro')
lr_recall = recall_score(y_test, y_pred_lr, average='macro')
lr_f1 = f1_score(y_test, y_pred_lr, average='macro')

print("Logistic Regression 结果:")
print(f"准确率: {lr_accuracy:.4f}")
print(f"精确率: {lr_precision:.4f}")
print(f"召回率: {lr_recall:.4f}")
print(f"F1分数: {lr_f1:.4f}")

# 混淆矩阵可视化
cm_lr = confusion_matrix(y_test, y_pred_lr)
plt.figure(figsize=(8, 6))
sns.heatmap(cm_lr, annot=True, fmt='d', cmap='Blues',
            xticklabels=[f'公司{i}' for i in range(1, 7)],
            yticklabels=[f'公司{i}' for i in range(1, 7)])
plt.title('Logistic Regression 混淆矩阵')
plt.xlabel('溯源标签')
plt.ylabel('真实标签')
plt.tight_layout()
plt.show()

# 特征重要性（系数）
feature_importance_lr = np.abs(lr_model.coef_).mean(axis=0)
importance_df_lr = pd.DataFrame({
    '特征': feature_cols,
    '重要性': feature_importance_lr
}).sort_values('重要性', ascending=True)

plt.figure(figsize=(10, 6))
plt.rcParams.update({
    'font.size': 12,
    'axes.titlesize': 12,
    'axes.labelsize': 12,
    'xtick.labelsize': 12,
    'ytick.labelsize': 12,
    'legend.fontsize': 12,
})
plt.barh(importance_df_lr['特征'], importance_df_lr['重要性'])
plt.title('Logistic Regression 特征重要性')
plt.xlabel('重要性（系数绝对值）')
plt.tight_layout()
plt.show()

# cell6 MLP
from sklearn.neural_network import MLPClassifier

# 训练MLP模型
mlp_model = MLPClassifier(hidden_layer_sizes=(64, 32), activation='relu',
                         solver='adam', alpha=1e-4, learning_rate_init=1e-3,
                         max_iter=500, random_state=42)
mlp_model.fit(X_train_scaled, y_train)

# 预测
y_pred_mlp = mlp_model.predict(X_test_scaled)
y_proba_mlp = mlp_model.predict_proba(X_test_scaled)

# 评估指标
mlp_accuracy = accuracy_score(y_test, y_pred_mlp)
mlp_precision = precision_score(y_test, y_pred_mlp, average='macro')
mlp_recall = recall_score(y_test, y_pred_mlp, average='macro')
mlp_f1 = f1_score(y_test, y_pred_mlp, average='macro')

print("MLP Neural Network 结果:")
print(f"准确率: {mlp_accuracy:.4f}")
print(f"精确率: {mlp_precision:.4f}")
print(f"召回率: {mlp_recall:.4f}")
print(f"F1分数: {mlp_f1:.4f}")

# 混淆矩阵可视化
cm_mlp = confusion_matrix(y_test, y_pred_mlp)
plt.figure(figsize=(8, 6))

sns.heatmap(cm_mlp, annot=True, fmt='d', cmap='Oranges',
            xticklabels=[f'公司{i}' for i in range(1, 7)],
            yticklabels=[f'公司{i}' for i in range(1, 7)])
plt.title('MLP Neural Network 混淆矩阵')
plt.xlabel('溯源标签')
plt.ylabel('真实标签')
plt.tight_layout()
plt.show()

# 训练损失曲线
if hasattr(mlp_model, 'loss_curve_'):
    plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 14,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14,
})
    plt.figure(figsize=(10, 6))
    plt.plot(mlp_model.loss_curve_)
    plt.title('MLP 训练损失曲线')
    plt.xlabel('迭代次数')
    plt.ylabel('损失值')
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


# cell7 KMeans
from sklearn.cluster import KMeans

# 训练K-means模型
kmeans_model = KMeans(n_clusters=6, random_state=42)
cluster_labels = kmeans_model.fit_predict(X_train_scaled)

# 建立聚类到标签的映射
cluster_to_label = {}
for cluster_id in range(6):
    cluster_indices = np.where(cluster_labels == cluster_id)[0]
    if len(cluster_indices) > 0:
        most_common_label = np.bincount(y_train.iloc[cluster_indices]).argmax()
        cluster_to_label[cluster_id] = most_common_label
    else:
        cluster_to_label[cluster_id] = 1

# 在测试集上预测
test_clusters = kmeans_model.predict(X_test_scaled)
y_pred_kmeans = np.array([cluster_to_label[cluster] for cluster in test_clusters])

# 评估指标
kmeans_accuracy = accuracy_score(y_test, y_pred_kmeans)
kmeans_precision = precision_score(y_test, y_pred_kmeans, average='macro')
kmeans_recall = recall_score(y_test, y_pred_kmeans, average='macro')
kmeans_f1 = f1_score(y_test, y_pred_kmeans, average='macro')

print("K-Means Clustering 结果:")
print(f"准确率: {kmeans_accuracy:.4f}")
print(f"精确率: {kmeans_precision:.4f}")
print(f"召回率: {kmeans_recall:.4f}")
print(f"F1分数: {kmeans_f1:.4f}")

# 混淆矩阵可视化
cm_kmeans = confusion_matrix(y_test, y_pred_kmeans)
plt.figure(figsize=(8, 6))
sns.heatmap(cm_kmeans, annot=True, fmt='d', cmap='Purples',
            xticklabels=[f'公司{i}' for i in range(1, 7)],
            yticklabels=[f'公司{i}' for i in range(1, 7)])
plt.title('K-Means Clustering 混淆矩阵')
plt.xlabel('溯源标签')
plt.ylabel('真实标签')
plt.tight_layout()
plt.show()

# 聚类中心可视化
cluster_centers = kmeans_model.cluster_centers_
plt.figure(figsize=(12, 8))
plt.rcParams.update({
    'font.size': 14,
    'axes.titlesize': 14,
    'axes.labelsize': 14,
    'xtick.labelsize': 14,
    'ytick.labelsize': 14,
    'legend.fontsize': 14,
})

im = plt.imshow(cluster_centers.T, cmap='viridis', aspect='auto')
plt.colorbar(im)
plt.xlabel('聚类中心')
plt.ylabel('特征')
plt.title('K-Means 聚类中心特征分布')
plt.yticks(range(len(feature_cols)), feature_cols)
plt.xticks(range(6), [f'聚类{i}' for i in range(6)])
plt.tight_layout()
plt.show()

#cell8  决策树
from xgboost import XGBClassifier
from sklearn.ensemble import RandomForestClassifier, VotingClassifier
from sklearn.preprocessing import LabelEncoder

# 标签编码（XGBoost需要从0开始的连续标签）
label_encoder = LabelEncoder()
y_train_encoded = label_encoder.fit_transform(y_train)
y_test_encoded = label_encoder.transform(y_test)

# 训练XGBoost
xgb_model = XGBClassifier(n_estimators=100, learning_rate=0.1, max_depth=6,
                         eval_metric='mlogloss', random_state=42)
xgb_model.fit(X_train_scaled, y_train_encoded)

# 训练Random Forest
rf_model = RandomForestClassifier(n_estimators=100, max_depth=None, random_state=42)
rf_model.fit(X_train_scaled, y_train_encoded)

# 集成模型（软投票）
ensemble_model = VotingClassifier(
    estimators=[('xgb', xgb_model), ('rf', rf_model)],
    voting='soft'
)
ensemble_model.fit(X_train_scaled, y_train_encoded)

# 预测
y_pred_xgb = label_encoder.inverse_transform(xgb_model.predict(X_test_scaled))
y_pred_rf = label_encoder.inverse_transform(rf_model.predict(X_test_scaled))
y_pred_ensemble = label_encoder.inverse_transform(ensemble_model.predict(X_test_scaled))
y_proba_ensemble = ensemble_model.predict_proba(X_test_scaled)

# 评估各模型
models_results = {}
for name, y_pred in [('XGBoost', y_pred_xgb), ('Random Forest', y_pred_rf), ('Ensemble', y_pred_ensemble)]:
    acc = accuracy_score(y_test, y_pred)
    prec = precision_score(y_test, y_pred, average='macro')
    rec = recall_score(y_test, y_pred, average='macro')
    f1 = f1_score(y_test, y_pred, average='macro')
    models_results[name] = {'accuracy': acc, 'precision': prec, 'recall': rec, 'f1': f1}

print("决策树集成结果对比:")
for model, metrics in models_results.items():
    print(f"{model}:")
    for metric, value in metrics.items():
        print(f"  {metric}: {value:.4f}")
    print()

# 集成模型混淆矩阵
cm_ensemble = confusion_matrix(y_test, y_pred_ensemble)
plt.figure(figsize=(8, 6))
sns.heatmap(cm_ensemble, annot=True, fmt='d', cmap='Greens',
            xticklabels=[f'公司{i}' for i in range(1, 7)],
            yticklabels=[f'公司{i}' for i in range(1, 7)])
plt.title('XGBoost + RF 集成模型混淆矩阵')
plt.xlabel('预测标签')
plt.ylabel('真实标签')
plt.tight_layout()
plt.show()

# XGBoost特征重要性
feature_importance_xgb = xgb_model.feature_importances_
importance_df_xgb = pd.DataFrame({
    '特征': feature_cols,
    '重要性': feature_importance_xgb
}).sort_values('重要性', ascending=True)

plt.figure(figsize=(10, 6))
plt.barh(importance_df_xgb['特征'], importance_df_xgb['重要性'])
plt.title('XGBoost 特征重要性')
plt.xlabel('重要性')
plt.tight_layout()
plt.show()

#cell9 结果分析
# 汇总所有模型结果
all_models_results = {
    'Logistic Regression': {
        'accuracy': lr_accuracy, 'precision': lr_precision,
        'recall': lr_recall, 'f1': lr_f1
    },
    'MLP Neural Network': {
        'accuracy': mlp_accuracy, 'precision': mlp_precision,
        'recall': mlp_recall, 'f1': mlp_f1
    },
    'K-Means Clustering': {
        'accuracy': kmeans_accuracy, 'precision': kmeans_precision,
        'recall': kmeans_recall, 'f1': kmeans_f1
    },
    'XGBoost': models_results['XGBoost'],
    'Random Forest': models_results['Random Forest'],
    'Ensemble': models_results['Ensemble']
}

# 性能对比可视化
metrics_df = pd.DataFrame(all_models_results).T
print("所有模型性能对比:")
print(metrics_df.round(4))

# 绘制性能对比图
fig, axes = plt.subplots(2, 2, figsize=(15, 10))
metrics = ['accuracy', 'precision', 'recall', 'f1']
for i, metric in enumerate(metrics):
    ax = axes[i//2, i%2]
    models = list(all_models_results.keys())
    values = [all_models_results[model][metric] for model in models]
    bars = ax.bar(models, values, color=['blue', 'orange', 'purple', 'green', 'red', 'brown'])
    ax.set_title(f'{metric.upper()} 对比')
    ax.set_ylabel(metric.upper())
    ax.set_ylim(0, 1)
    plt.setp(ax.get_xticklabels(), rotation=45, ha='right')
    # 添加数值标签
    for bar, value in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.01,
                f'{value:.3f}', ha='center', va='bottom')

plt.tight_layout()
plt.show()

# 最佳模型总结
best_model = max(all_models_results.items(), key=lambda x: x[1]['accuracy'])
print(f"\n最佳模型: {best_model[0]}")
print(f"准确率: {best_model[1]['accuracy']:.4f}")

# 保存结果
results_summary = {
    'best_model': best_model[0],
    'best_accuracy': best_model[1]['accuracy'],
    'all_results': all_models_results
}

import json
with open('./data/model_results.json', 'w', encoding='utf-8') as f:
    json.dump(results_summary, f, ensure_ascii=False, indent=2)

import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
import pandas as pd
from sklearn.metrics import roc_curve, auc
from sklearn.preprocessing import label_binarize
from sklearn.manifold import TSNE

plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
plt.rcParams['axes.unicode_minus'] = False

# 1. 性能雷达图
metrics = ['accuracy', 'precision', 'recall', 'f1']
models = list(all_models_results.keys())
angles = np.linspace(0, 2 * np.pi, len(metrics), endpoint=False).tolist()
angles += angles[:1]

fig, ax = plt.subplots(figsize=(10, 8), subplot_kw=dict(projection='polar'))
colors = ['red', 'blue', 'green', 'orange', 'purple', 'brown']

for i, model in enumerate(models):
    values = [all_models_results[model][m] for m in metrics]
    values += values[:1]
    ax.plot(angles, values, 'o-', linewidth=2, label=model, color=colors[i])
    ax.fill(angles, values, alpha=0.1, color=colors[i])

ax.set_xticks(angles[:-1])
ax.set_xticklabels([m.upper() for m in metrics])
ax.set_ylim(0, 1)
ax.set_title('模型性能雷达图', fontsize=16, fontweight='bold')
ax.legend(bbox_to_anchor=(1.3, 1.0))
plt.tight_layout()
plt.show()

# 2. 特征重要性热力图
importance_data = {
    'XGBoost': xgb_model.feature_importances_,
    'Random Forest': rf_model.feature_importances_,
    'Logistic': np.abs(lr_model.coef_).mean(axis=0)
}

for model in importance_data:
    importance_data[model] = importance_data[model] / np.max(importance_data[model])

importance_df = pd.DataFrame(importance_data, index=feature_cols)
plt.figure(figsize=(10, 8))
sns.heatmap(importance_df, annot=True, cmap='YlOrRd', fmt='.2f')
plt.title('特征重要性对比')
plt.tight_layout()
plt.show()

# 3. ROC曲线对比
def align_proba_columns(y_proba, source_classes, target_classes):
    aligned = np.zeros((y_proba.shape[0], len(target_classes)))
    source_index = {cls: idx for idx, cls in enumerate(source_classes)}
    for idx, cls in enumerate(target_classes):
        if cls in source_index:
            aligned[:, idx] = y_proba[:, source_index[cls]]
    return aligned


def build_kmeans_scores(kmeans_model, cluster_to_label, X_scaled, target_classes):
    distances = kmeans_model.transform(X_scaled)
    similarities = np.zeros((X_scaled.shape[0], len(target_classes)))
    fallback_distance = distances.max(axis=1) + 1e-6

    for class_idx, class_label in enumerate(target_classes):
        cluster_ids = [
            cluster_id
            for cluster_id, mapped_label in cluster_to_label.items()
            if mapped_label == class_label
        ]
        if cluster_ids:
            class_distance = distances[:, cluster_ids].min(axis=1)
        else:
            class_distance = fallback_distance
        similarities[:, class_idx] = 1.0 / (class_distance + 1e-6)

    row_sums = similarities.sum(axis=1, keepdims=True)
    row_sums[row_sums == 0] = 1.0
    return similarities / row_sums


target_classes = np.array(label_encoder.classes_)
y_test_bin = label_binarize(y_test, classes=target_classes)

y_proba_lr_aligned = align_proba_columns(y_proba_lr, lr_model.classes_, target_classes)
y_proba_mlp_aligned = align_proba_columns(y_proba_mlp, mlp_model.classes_, target_classes)
y_proba_kmeans = build_kmeans_scores(kmeans_model, cluster_to_label, X_test_scaled, target_classes)
y_proba_xgb = align_proba_columns(xgb_model.predict_proba(X_test_scaled), target_classes, target_classes)
y_proba_rf = align_proba_columns(rf_model.predict_proba(X_test_scaled), target_classes, target_classes)
y_proba_ensemble_aligned = align_proba_columns(y_proba_ensemble, target_classes, target_classes)

models_proba = [
    ('Logistic Regression', y_proba_lr_aligned),
    ('MLP Neural Network', y_proba_mlp_aligned),
    ('K-Means Clustering', y_proba_kmeans),
    ('XGBoost', y_proba_xgb),
    ('Random Forest', y_proba_rf),
    ('Ensemble', y_proba_ensemble_aligned)
]

plt.figure(figsize=(12, 9))
for model_name, y_proba in models_proba:
    fpr = dict()
    tpr = dict()
    for i in range(len(target_classes)):
        fpr[i], tpr[i], _ = roc_curve(y_test_bin[:, i], y_proba[:, i])

    all_fpr = np.unique(np.concatenate([fpr[i] for i in range(len(target_classes))]))
    mean_tpr = np.zeros_like(all_fpr)
    for i in range(len(target_classes)):
        mean_tpr += np.interp(all_fpr, fpr[i], tpr[i])
    mean_tpr /= len(target_classes)

    macro_auc = auc(all_fpr, mean_tpr)
    plt.plot(all_fpr, mean_tpr, linewidth=2, label=f'{model_name} (AUC={macro_auc:.3f})')

plt.plot([0, 1], [0, 1], 'k--')
plt.xlabel('假正率')
plt.ylabel('真正率')
plt.title('ROC曲线对比')
plt.legend()
plt.grid(True, alpha=0.3)
plt.show()

# 4. t-SNE可视化
tsne = TSNE(n_components=2, random_state=42)
X_tsne = tsne.fit_transform(X_test_scaled)

fig, axes = plt.subplots(2, 3, figsize=(15, 10))
axes = axes.flatten()

scatter = axes[0].scatter(X_tsne[:, 0], X_tsne[:, 1], c=y_test, cmap='tab10')
axes[0].set_title('真实标签')
plt.colorbar(scatter, ax=axes[0])

predictions = [y_pred_lr, y_pred_mlp, y_pred_kmeans, y_pred_xgb, y_pred_ensemble]
names = ['Logistic', 'MLP', 'K-Means', 'XGBoost', 'Ensemble']

for i, (pred, name) in enumerate(zip(predictions, names)):
    scatter = axes[i + 1].scatter(X_tsne[:, 0], X_tsne[:, 1], c=pred, cmap='tab10')
    axes[i + 1].set_title(f'{name}预测')
    plt.colorbar(scatter, ax=axes[i + 1])

plt.tight_layout()
plt.show()

# 5. 性能对比柱状图
metrics_names = ['accuracy', 'precision', 'recall', 'f1']
x = np.arange(len(metrics_names))
width = 0.12

fig, ax = plt.subplots(figsize=(12, 8))
for i, model in enumerate(models):
    values = [all_models_results[model][m] for m in metrics_names]
    ax.bar(x + i * width, values, width, label=model)

ax.set_xlabel('性能指标')
ax.set_ylabel('分数')
ax.set_title('模型性能对比')
ax.set_xticks(x + width * 2.5)
ax.set_xticklabels([m.upper() for m in metrics_names])
ax.legend()
ax.grid(True, alpha=0.3)
plt.tight_layout()
plt.show()

# 6. 总结表格
table_data = []
for model in all_models_results.keys():
    row = [model] + [f'{all_models_results[model][m]:.4f}' for m in metrics_names]
    table_data.append(row)

fig, ax = plt.subplots(figsize=(10, 6))
ax.axis('off')
table = ax.table(cellText=table_data,
                 colLabels=['模型'] + [m.upper() for m in metrics_names],
                 cellLoc='center', loc='center')
table.auto_set_font_size(False)
table.set_fontsize(12)
table.scale(1.2, 2)

for i in range(len(metrics_names) + 1):
    table[(0, i)].set_facecolor('#4CAF50')
    table[(0, i)].set_text_props(weight='bold', color='white')

ax.set_title('模型性能汇总表', fontsize=16, fontweight='bold')
plt.tight_layout()
plt.show()

# 最终总结
best_model = max(all_models_results.items(), key=lambda x: x[1]['accuracy'])
print(f"最佳模型: {best_model[0]}")
print(f"最高准确率: {best_model[1]['accuracy']:.4f}")

sorted_models = sorted(all_models_results.items(), key=lambda x: x[1]['accuracy'], reverse=True)
print("模型排名:")
for i, (model, metrics) in enumerate(sorted_models, 1):
    print(f"{i}. {model}: {metrics['accuracy']:.4f}")
