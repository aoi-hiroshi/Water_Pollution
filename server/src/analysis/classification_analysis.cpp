#include "water/analysis/classification_analysis.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace water::analysis {
namespace {
using namespace domain;

double quantile(const std::vector<double>& sorted, double probability) {
    const long double position = probability * static_cast<long double>(sorted.size() - 1);
    const auto left = static_cast<std::size_t>(position);
    const auto right = std::min(left + 1, sorted.size() - 1);
    const auto fraction = position - left;
    return static_cast<double>((1 - fraction) * sorted[left] + fraction * sorted[right]);
}

std::vector<double> values(const std::vector<WaterSample>& samples,
                          const ClassificationFeature& feature) {
    std::vector<double> result;
    result.reserve(samples.size());
    for (const auto& row : samples) {
        if (const auto& value = row.*feature.member; value) { result.push_back(*value); }
    }
    std::sort(result.begin(), result.end());
    return result;
}

void summarize(ClassificationFeatureStats& stats, const std::vector<double>& sorted) {
    stats.valid_count = sorted.size();
    if (sorted.empty()) { return; }
    long double mean = 0, m2 = 0;
    std::size_t count = 0;
    for (const auto value : sorted) {
        const auto delta = value - mean;
        mean += delta / ++count;
        m2 += delta * (value - mean);
    }
    stats.mean = static_cast<double>(mean);
    stats.standard_deviation = static_cast<double>(std::sqrt(std::max(0.0L, m2 / count)));
    stats.minimum = sorted.front(); stats.maximum = sorted.back();
    stats.q10 = quantile(sorted, .1); stats.q25 = quantile(sorted, .25);
    stats.median = quantile(sorted, .5); stats.q75 = quantile(sorted, .75);
    stats.q90 = quantile(sorted, .9);
}

std::string companyName(std::int64_t id, const std::vector<Company>& companies) {
    const auto match = std::find_if(companies.begin(), companies.end(),
                                  [id](const auto& company) { return company.id == id; });
    return match == companies.end() ? "公司" + std::to_string(id) : match->name;
}

std::vector<double> ranks(const std::vector<double>& source) {
    std::vector<std::pair<double, std::size_t>> indexed;
    for (std::size_t index = 0; index < source.size(); ++index) {
        indexed.emplace_back(source[index], index);
    }
    std::sort(indexed.begin(), indexed.end());
    std::vector<double> result(source.size());
    for (std::size_t start = 0; start < indexed.size();) {
        auto end = start + 1;
        while (end < indexed.size() && indexed[end].first == indexed[start].first) { ++end; }
        const double rank = (static_cast<double>(start) + end - 1) / 2 + 1;
        for (auto index = start; index < end; ++index) { result[indexed[index].second] = rank; }
        start = end;
    }
    return result;
}

std::optional<double> pearson(const std::vector<double>& first,
                              const std::vector<double>& second) {
    if (first.size() < 2) { return std::nullopt; }
    long double mean_x = 0, mean_y = 0, variance_x = 0, variance_y = 0, covariance = 0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const long double dx = first[index] - mean_x, dy = second[index] - mean_y;
        mean_x += dx / (index + 1); mean_y += dy / (index + 1);
        variance_x += dx * (first[index] - mean_x);
        variance_y += dy * (second[index] - mean_y);
        covariance += dx * (second[index] - mean_y);
    }
    if (variance_x <= 0 || variance_y <= 0) { return std::nullopt; }
    const auto value = covariance / std::sqrt(variance_x) / std::sqrt(variance_y);
    if (!std::isfinite(value)) { return std::nullopt; }
    return std::clamp(static_cast<double>(value), -1.0, 1.0);
}

void correlation(ClassificationDataResult& result, const ClassificationDataRequest& request) {
    for (std::size_t first = 0; first < 10; ++first) {
        for (auto second = first; second < 10; ++second) {
            std::vector<double> x, y;
            for (const auto& row : result.processed_rows) {
                const auto& a = row.*kClassificationFeatures[first].member;
                const auto& b = row.*kClassificationFeatures[second].member;
                if (a && b) { x.push_back(*a); y.push_back(*b); }
            }
            result.pair_counts[first][second] = result.pair_counts[second][first] = x.size();
            if (request.method == "spearman") { x = ranks(x); y = ranks(y); }
            const auto coefficient = pearson(x, y);
            result.correlation[first][second] = result.correlation[second][first] = coefficient;
            if (first != second && coefficient && std::abs(*coefficient) > .7) {
                result.high_correlations.push_back({kClassificationFeatures[first].field,
                    kClassificationFeatures[second].field, *coefficient, x.size()});
            }
        }
    }
    result.rule = request.method + "_pairwise_complete_v1";
    result.warnings.push_back("相关性不是因果关系；每对特征使用共同非空样本，常量或不足2条返回null。");
}

void distribution(ClassificationDataResult& result, const ClassificationDataRequest& request,
                  const std::vector<Company>& companies) {
    const auto feature = std::find_if(kClassificationFeatures.begin(), kClassificationFeatures.end(),
                                    [&request](const auto& item) { return item.field == request.feature; });
    std::map<std::int64_t, std::vector<double>> grouped;
    std::map<std::int64_t, std::size_t> missing;
    std::vector<double> all;
    for (const auto& row : result.processed_rows) {
        auto& group = grouped[row.company_id];
        const auto& value = row.*feature->member;
        if (value) { group.push_back(*value); all.push_back(*value); }
        else { ++missing[row.company_id]; }
    }
    std::sort(all.begin(), all.end());
    if (grouped.size() > kClassificationGroupLimit) {
        throw std::length_error("company comparison exceeds 32 groups; narrow the dataset");
    }
    for (auto& [id, group] : grouped) {
        std::sort(group.begin(), group.end());
        DistributionGroup item;
        item.company_id = id; item.company_name = companyName(id, companies);
        item.valid_count = group.size(); item.missing_count = missing[id];
        if (!group.empty()) {
            item.minimum = group.front(); item.maximum = group.back();
            item.q10 = quantile(group, .1); item.q25 = quantile(group, .25);
            item.median = quantile(group, .5); item.q75 = quantile(group, .75);
            item.q90 = quantile(group, .9);
            const long double iqr = static_cast<long double>(*item.q75) - *item.q25;
            const long double lower = *item.q25 - 1.5L * iqr, upper = *item.q75 + 1.5L * iqr;
            for (const double value : group) {
                if (value < lower || value > upper) { ++item.outlier_count; }
                else {
                    if (!item.lower_whisker) { item.lower_whisker = value; }
                    item.upper_whisker = value;
                }
            }
        }
        if (!all.empty()) {
            const long double low = all.front(), high = all.back();
            const auto count = low == high ? std::size_t{1} : request.bins;
            for (std::size_t index = 0; index < count; ++index) {
                item.histogram.push_back({static_cast<double>(low + (high-low) * index/count),
                    static_cast<double>(low + (high-low) * (index+1)/count), 0});
            }
            for (const auto value : group) {
                const auto index = low == high ? 0 : std::min(count - 1,
                    static_cast<std::size_t>((value-low)/(high-low)*count));
                ++item.histogram[index].count;
            }
        }
        result.groups.push_back(std::move(item));
    }
    result.rule = "linear_quantile_tukey_box_common_bins_v1";
    result.warnings.push_back("分布统计忽略空值；公司对比使用相同分箱边界，频数不代表概率。");
}
} // namespace

void validateClassificationRequest(const domain::ClassificationDataRequest& request) {
    if (request.company_id <= 0 || request.cleaning_run_id < 0 ||
        (request.dataset != "train_data" && request.dataset != "test_data")) {
        throw std::invalid_argument("invalid company_id, cleaning_run_id or dataset");
    }
    if (request.operation != "missing" && request.operation != "clean" &&
        request.operation != "distribution" && request.operation != "correlation") {
        throw std::invalid_argument("unsupported classification data operation");
    }
    if (request.window < 2 || request.window > 500 || !std::isfinite(request.threshold) ||
        request.threshold <= 0 || request.threshold > 20 || request.bins < 5 || request.bins > 100) {
        throw std::invalid_argument("window, threshold or bins out of range");
    }
    if (request.method != "pearson" && request.method != "spearman") {
        throw std::invalid_argument("method must be pearson or spearman");
    }
    if (request.view != "histogram" && request.view != "boxplot" && request.view != "comparison") {
        throw std::invalid_argument("unsupported distribution view");
    }
    const auto& features = domain::kClassificationFeatures;
    if (std::none_of(features.begin(), features.end(), [&request](const auto& item) {
            return item.field == request.feature; })) {
        throw std::invalid_argument("unsupported feature");
    }
    if (request.persist && request.operation != "missing" && request.operation != "clean") {
        throw std::invalid_argument("only missing/clean can be persisted");
    }
    if (request.operation == "distribution" && request.view == "comparison" && request.cleaning_run_id != 0) {
        throw std::invalid_argument("company comparison requires raw source (cleaning_run_id=0)");
    }
}

domain::ClassificationDataResult analyzeClassificationData(
    const domain::ClassificationDataRequest& request, std::vector<domain::WaterSample> samples,
    const std::vector<domain::Company>& companies) {
    validateClassificationRequest(request);
    if (samples.empty() || samples.size() > domain::kClassificationRowLimit) {
        throw std::invalid_argument("empty or oversized classification dataset");
    }
    const bool comparison = request.operation == "distribution" && request.view == "comparison";
    std::map<std::int64_t, std::int64_t> previous_id;
    for (const auto& row : samples) {
        if (row.id <= 0 || row.company_id <= 0 || row.id <= previous_id[row.company_id] ||
            (!comparison && row.company_id != request.company_id)) {
            throw std::invalid_argument("invalid, duplicate, unordered or cross-company sample");
        }
        previous_id[row.company_id] = row.id;
        for (const auto& feature : domain::kClassificationFeatures) {
            const auto& value = row.*feature.member;
            if (value && !std::isfinite(*value)) { throw std::invalid_argument("non-finite feature value"); }
        }
    }
    domain::ClassificationDataResult result;
    result.company_id = request.company_id; result.company_name = companyName(request.company_id, companies);
    result.dataset = request.dataset; result.operation = request.operation;
    result.input_cleaning_run_id = request.cleaning_run_id; result.sample_count = samples.size();
    result.original_rows = std::move(samples); result.processed_rows = result.original_rows;
    const bool repair = request.operation == "missing" || request.operation == "clean";
    for (const auto& feature : domain::kClassificationFeatures) {
        domain::ClassificationFeatureStats stats;
        stats.field = feature.field; stats.label = feature.label; stats.unit = feature.unit;
        auto sorted = values(result.original_rows, feature);
        stats.missing_before = result.sample_count - sorted.size();
        if (repair && !sorted.empty()) {
            const auto median = quantile(sorted, .5);
            for (auto& row : result.processed_rows) {
                auto& value = row.*feature.member;
                if (!value) { value = median; ++stats.filled_count; }
            }
        }
        if (repair && sorted.empty()) { result.warnings.push_back(stats.label + "整列为空，未凭空填充。"); }
        if (request.operation == "clean" && !sorted.empty()) {
            // Detection uses the unchanged median-repaired series, like the
            // notebook detecting on raw data before applying its fill rule.
            std::vector<double> source;
            for (const auto& row : result.processed_rows) { source.push_back(*(row.*feature.member)); }
            for (std::size_t index = request.window; index < source.size(); ++index) {
                long double mean = 0, m2 = 0;
                for (std::size_t offset = 0; offset < request.window; ++offset) {
                    const auto delta = source[index-request.window+offset] - mean;
                    mean += delta / (offset+1);
                    m2 += delta * (source[index-request.window+offset] - mean);
                }
                const auto sigma = std::sqrt(std::max(0.0L, m2/request.window));
                const auto difference = std::abs(source[index]-mean);
                const bool outlier = sigma == 0 ? difference > 0 : difference/sigma > request.threshold;
                if (outlier) {
                    result.processed_rows[index].*feature.member = result.processed_rows[index-1].*feature.member;
                    ++stats.outlier_count;
                }
            }
        }
        for (std::size_t index = 0; index < result.sample_count; ++index) {
            const auto& before = result.original_rows[index].*feature.member;
            const auto& after = result.processed_rows[index].*feature.member;
            if (before != after) { ++stats.changed_count; }
        }
        sorted = values(result.processed_rows, feature);
        stats.missing_after = result.sample_count - sorted.size();
        summarize(stats, sorted); result.features.push_back(std::move(stats));
    }
    if (repair) {
        result.rule = request.operation == "missing" ? "company_median_v1" : "median_sliding_zscore_forward_hold_v1";
        result.warnings.push_back("按公司、样本ID顺序处理；当前没有采样时间，不能解释成真实时间滤波。");
        result.warnings.push_back("中位数修复使用本次完整数据集，属于离线处理，不作为因果在线清洗。");
        if (request.operation == "clean") {
            result.warnings.push_back("复现Notebook的简化修复（异常点取前一个修复值），不宣称完整卡尔曼滤波。");
            if (result.sample_count <= request.window) { result.warnings.push_back("样本不足滑动窗口，未检测异常点。"); }
        }
    } else if (request.operation == "distribution") { distribution(result, request, companies); }
    else { correlation(result, request); }
    const auto preview = std::min(std::size_t{50}, result.sample_count);
    result.preview_before.assign(result.original_rows.begin(), result.original_rows.begin()+preview);
    result.preview_after.assign(result.processed_rows.begin(), result.processed_rows.begin()+preview);
    const auto chart = std::min(domain::kClassificationChartLimit, result.sample_count);
    for (std::size_t index = 0; index < chart; ++index) {
        const auto source = chart == 1 ? 0 : index*(result.sample_count-1)/(chart-1);
        result.chart_before.push_back(result.original_rows[source]);
        result.chart_after.push_back(result.processed_rows[source]);
    }
    if (chart < result.sample_count) { result.warnings.push_back("曲线为等间隔抽样预览；统计使用全部样本，不能据预览排除未展示异常点。"); }
    return result;
}
} // namespace water::analysis
