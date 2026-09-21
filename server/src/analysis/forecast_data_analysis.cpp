#include "water/analysis/forecast_data_analysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace water::analysis {
namespace {
using namespace domain;

const ClassificationFeature& featureDefinition(const std::string& field) {
    const auto match = std::find_if(kClassificationFeatures.begin(),
        kClassificationFeatures.end(), [&field](const auto& item) {
            return item.field == field;
        });
    if (match == kClassificationFeatures.end()) {
        throw std::invalid_argument("unsupported forecast feature");
    }
    return *match;
}

double quantile(const std::vector<double>& sorted, double probability) {
    const long double position = probability * static_cast<long double>(sorted.size() - 1);
    const auto left = static_cast<std::size_t>(position);
    const auto right = std::min(left + 1, sorted.size() - 1);
    const auto fraction = position - left;
    return static_cast<double>((1 - fraction) * sorted[left] + fraction * sorted[right]);
}

void summarize(ClassificationFeatureStats& stats, std::vector<double> values) {
    stats.valid_count = values.size();
    if (values.empty()) {
        return;
    }
    long double mean = 0;
    long double m2 = 0;
    std::size_t count = 0;
    for (const double value : values) {
        const auto delta = value - mean;
        mean += delta / ++count;
        m2 += delta * (value - mean);
    }
    std::sort(values.begin(), values.end());
    stats.mean = static_cast<double>(mean);
    stats.standard_deviation = static_cast<double>(std::sqrt(std::max(0.0L, m2 / count)));
    stats.minimum = values.front();
    stats.maximum = values.back();
    stats.q10 = quantile(values, .1);
    stats.q25 = quantile(values, .25);
    stats.median = quantile(values, .5);
    stats.q75 = quantile(values, .75);
    stats.q90 = quantile(values, .9);
}

std::vector<double> featureValues(const std::vector<WaterSample>& samples,
                                  const ClassificationFeature& feature) {
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        const auto& value = sample.*feature.member;
        if (value) {
            values.push_back(*value);
        }
    }
    return values;
}

std::size_t interpolate(std::vector<WaterSample>& samples,
                        const ClassificationFeature& feature) {
    std::vector<std::size_t> known;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (samples[index].*feature.member) {
            known.push_back(index);
        }
    }
    if (known.empty()) {
        return 0;
    }

    std::size_t filled = 0;
    const double first = *(samples[known.front()].*feature.member);
    for (std::size_t index = 0; index < known.front(); ++index) {
        samples[index].*feature.member = first;
        ++filled;
    }
    for (std::size_t pair = 1; pair < known.size(); ++pair) {
        const auto left = known[pair - 1];
        const auto right = known[pair];
        const double left_value = *(samples[left].*feature.member);
        const double right_value = *(samples[right].*feature.member);
        for (std::size_t index = left + 1; index < right; ++index) {
            const double ratio = static_cast<double>(index - left) /
                                 static_cast<double>(right - left);
            samples[index].*feature.member = left_value + ratio * (right_value - left_value);
            ++filled;
        }
    }
    const double last = *(samples[known.back()].*feature.member);
    for (std::size_t index = known.back() + 1; index < samples.size(); ++index) {
        samples[index].*feature.member = last;
        ++filled;
    }
    return filled;
}

std::size_t smooth(std::vector<WaterSample>& samples,
                   const ClassificationFeature& feature,
                   std::size_t window) {
    std::vector<std::optional<double>> source;
    source.reserve(samples.size());
    for (const auto& sample : samples) {
        source.push_back(sample.*feature.member);
    }
    long double sum = 0;
    std::size_t valid = 0;
    std::size_t changed = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (source[index]) {
            sum += *source[index];
            ++valid;
        }
        if (index >= window && source[index - window]) {
            sum -= *source[index - window];
            --valid;
        }
        auto& destination = samples[index].*feature.member;
        if (destination && valid > 0) {
            const double average = static_cast<double>(sum / valid);
            if (std::abs(*destination - average) > 1e-12) {
                ++changed;
            }
            destination = average;
        }
    }
    return changed;
}

std::vector<WaterSample> sampledChart(const std::vector<WaterSample>& samples) {
    if (samples.size() <= kForecastDataChartLimit) {
        return samples;
    }
    std::vector<WaterSample> chart;
    chart.reserve(kForecastDataChartLimit);
    for (std::size_t index = 0; index < kForecastDataChartLimit; ++index) {
        const auto source = index * (samples.size() - 1) /
                            (kForecastDataChartLimit - 1);
        chart.push_back(samples[source]);
    }
    return chart;
}

std::optional<double> slope(const std::vector<double>& values) {
    if (values.size() < 2) {
        return std::nullopt;
    }
    const long double mean_x = static_cast<long double>(values.size() - 1) / 2;
    const long double mean_y = std::accumulate(values.begin(), values.end(), 0.0L) /
                               values.size();
    long double numerator = 0;
    long double denominator = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const long double dx = static_cast<long double>(index) - mean_x;
        numerator += dx * (values[index] - mean_y);
        denominator += dx * dx;
    }
    return denominator == 0 ? std::nullopt
                            : std::optional<double>{static_cast<double>(numerator / denominator)};
}

std::optional<double> autocorrelation(const std::vector<double>& values,
                                      std::size_t lag) {
    if (lag == 0 || values.size() <= lag + 1) {
        return std::nullopt;
    }
    const long double mean = std::accumulate(values.begin(), values.end(), 0.0L) /
                             values.size();
    long double covariance = 0;
    long double variance = 0;
    for (const double value : values) {
        const auto centered = value - mean;
        variance += centered * centered;
    }
    for (std::size_t index = lag; index < values.size(); ++index) {
        covariance += (values[index] - mean) * (values[index - lag] - mean);
    }
    return variance <= 0 ? std::nullopt
                         : std::optional<double>{static_cast<double>(covariance / variance)};
}

std::pair<double, double> meanVariance(const std::vector<double>& values,
                                      std::size_t begin,
                                      std::size_t end) {
    const long double mean = std::accumulate(values.begin() + begin,
        values.begin() + end, 0.0L) / (end - begin);
    long double variance = 0;
    for (std::size_t index = begin; index < end; ++index) {
        const long double delta = values[index] - mean;
        variance += delta * delta;
    }
    return {static_cast<double>(mean),
            static_cast<double>(variance / (end - begin))};
}

void addDiagnosticMetrics(ForecastDataResult& result,
                          const ForecastDataRequest& request,
                          const std::vector<WaterSample>& samples) {
    const auto& feature = featureDefinition(request.feature);
    const auto values = featureValues(samples, feature);
    if (values.size() < 4) {
        result.warnings.push_back("有效数据不足，无法完成预测前诊断。");
        return;
    }
    if (request.diagnostic == "trend") {
        result.metrics.push_back({"slope", "线性趋势斜率", slope(values), "每采样步"});
        result.metrics.push_back({"range_change", "首尾变化量",
                                  values.back() - values.front(), feature.unit});
        result.rule = "least_squares_linear_trend_v1";
    } else if (request.diagnostic == "seasonality") {
        const auto lag = std::min(request.window, values.size() / 2);
        result.metrics.push_back({"lag", "检查滞后步数", static_cast<double>(lag), "步"});
        result.metrics.push_back({"autocorrelation", "滞后自相关",
                                  autocorrelation(values, lag), "[-1,1]"});
        result.rule = "lag_autocorrelation_v1";
        result.warnings.push_back("周期性检查是指定窗口的自相关预检，不等同于完整频谱分析。");
    } else {
        const auto middle = values.size() / 2;
        const auto first = meanVariance(values, 0, middle);
        const auto second = meanVariance(values, middle, values.size());
        result.metrics.push_back({"mean_shift", "前后半段均值变化",
                                  second.first - first.first, feature.unit});
        result.metrics.push_back({"variance_ratio", "后段/前段方差比",
                                  first.second <= 0 ? std::nullopt
                                                    : std::optional<double>{second.second / first.second},
                                  "比值"});
        result.rule = "split_mean_variance_stationarity_check_v1";
        result.warnings.push_back("平稳性结果是均值/方差分段预检，不替代 ADF 等统计检验。");
    }
}
} // namespace

void validateForecastDataRequest(const ForecastDataRequest& request) {
    if (request.company_id != 7) {
        throw std::invalid_argument("forecast preprocessing is restricted to company_id 7");
    }
    if (request.dataset != "train_data" && request.dataset != "val_data" &&
        request.dataset != "test_data") {
        throw std::invalid_argument("dataset must be train_data, val_data or test_data");
    }
    if (request.operation != "sequence" && request.operation != "missing" &&
        request.operation != "smooth" && request.operation != "window" &&
        request.operation != "diagnosis") {
        throw std::invalid_argument("unsupported forecast data operation");
    }
    static_cast<void>(featureDefinition(request.feature));
    if (request.window < 2 || request.window > 1440 || request.horizon < 1 ||
        request.horizon > 120) {
        throw std::invalid_argument("window or horizon is out of range");
    }
    if (request.diagnostic != "trend" && request.diagnostic != "seasonality" &&
        request.diagnostic != "stationarity") {
        throw std::invalid_argument("unsupported forecast diagnostic");
    }
}

ForecastDataResult analyzeForecastData(const ForecastDataRequest& request,
                                       Company company,
                                       std::vector<WaterSample> samples) {
    validateForecastDataRequest(request);
    if (samples.empty() || samples.size() > kForecastDataRowLimit) {
        throw std::invalid_argument("empty or oversized forecast dataset");
    }
    if (company.id != request.company_id ||
        (company.task_type && *company.task_type != "forecast")) {
        throw std::invalid_argument("selected company is not the forecast company");
    }

    ForecastDataResult result;
    result.company_id = company.id;
    result.company_name = std::move(company.name);
    result.dataset = request.dataset;
    result.operation = request.operation;
    result.sample_count = samples.size();

    std::size_t non_monotonic_timestamps = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& row = samples[index];
        if (row.company_id != request.company_id || row.id <= 0 ||
            (index > 0 && row.sample_index <= samples[index - 1].sample_index)) {
            throw std::invalid_argument("forecast samples are unordered or cross-company");
        }
        if (index > 0 && row.sample_index != samples[index - 1].sample_index + 1) {
            ++result.sequence_gap_count;
        }
        if (row.sampled_at) {
            ++result.timestamp_count;
            if (index > 0 && samples[index - 1].sampled_at &&
                *row.sampled_at <= *samples[index - 1].sampled_at) {
                ++non_monotonic_timestamps;
            }
        }
        for (const auto& feature : kClassificationFeatures) {
            const auto& value = row.*feature.member;
            if (value && !std::isfinite(*value)) {
                throw std::invalid_argument("forecast dataset contains NaN or Inf");
            }
        }
    }

    const auto original = samples;
    std::size_t filled_total = 0;
    std::size_t smoothed_total = 0;
    if (request.operation == "missing" || request.operation == "smooth") {
        for (const auto& feature : kClassificationFeatures) {
            filled_total += interpolate(samples, feature);
        }
        result.rule = "linear_interpolation_edge_hold_v1";
    }
    if (request.operation == "smooth") {
        for (const auto& feature : kClassificationFeatures) {
            smoothed_total += smooth(samples, feature, request.window);
        }
        result.rule = "linear_interpolation_causal_moving_average_v1";
    }

    for (const auto& feature : kClassificationFeatures) {
        ClassificationFeatureStats stats;
        stats.field = feature.field;
        stats.label = feature.label;
        stats.unit = feature.unit;
        for (const auto& row : original) {
            if (!(row.*feature.member)) {
                ++stats.missing_before;
            }
        }
        for (std::size_t index = 0; index < samples.size(); ++index) {
            if (!(samples[index].*feature.member)) {
                ++stats.missing_after;
            }
            if (original[index].*feature.member != samples[index].*feature.member) {
                ++stats.changed_count;
            }
        }
        stats.filled_count = stats.missing_before - stats.missing_after;
        summarize(stats, featureValues(samples, feature));
        result.features.push_back(std::move(stats));
    }

    result.metrics.push_back({"sample_count", "样本总数",
                              static_cast<double>(result.sample_count), "条"});
    result.metrics.push_back({"timestamp_coverage", "时间戳覆盖率",
                              100.0 * result.timestamp_count / result.sample_count, "%"});
    result.metrics.push_back({"sequence_gaps", "序列断点数",
                              static_cast<double>(result.sequence_gap_count), "处"});
    if (request.operation == "sequence") {
        result.rule = "sample_index_and_timestamp_continuity_v1";
        result.metrics.push_back({"timestamp_order_errors", "时间逆序/重复",
                                  static_cast<double>(non_monotonic_timestamps), "处"});
    } else if (request.operation == "missing") {
        result.metrics.push_back({"filled_values", "插值修复数量",
                                  static_cast<double>(filled_total), "个值"});
        result.warnings.push_back("内部缺失使用线性插值，序列两端使用最近有效值；原始表不会被覆盖。");
    } else if (request.operation == "smooth") {
        result.metrics.push_back({"window", "平滑窗口", static_cast<double>(request.window), "步"});
        result.metrics.push_back({"smoothed_values", "平滑调整数量",
                                  static_cast<double>(smoothed_total), "个值"});
        result.warnings.push_back("采用因果移动平均，只使用当前及之前的样本，结果仅供预览且不覆盖原始表。");
    } else if (request.operation == "window") {
        const std::size_t lookback = 120;
        const std::size_t windows = samples.size() >= lookback + request.horizon
            ? samples.size() - lookback - request.horizon + 1 : 0;
        const auto selected = std::find_if(result.features.begin(), result.features.end(),
            [&request](const auto& item) { return item.field == request.feature; });
        result.rule = "lstm_sliding_window_preview_v1";
        result.metrics.push_back({"lookback", "输入窗口", static_cast<double>(lookback), "步"});
        result.metrics.push_back({"horizon", "预测长度", static_cast<double>(request.horizon), "步"});
        result.metrics.push_back({"window_count", "可构造窗口数", static_cast<double>(windows), "组"});
        if (selected != result.features.end()) {
            result.metrics.push_back({"feature_mean", selected->label + "均值",
                                      selected->mean, selected->unit});
            result.metrics.push_back({"feature_std", selected->label + "标准差",
                                      selected->standard_deviation, selected->unit});
            result.metrics.push_back({"feature_median", selected->label + "中位数",
                                      selected->median, selected->unit});
        }
        if (windows == 0) {
            result.warnings.push_back("当前数据不足以构造 120 步输入窗口和指定预测长度。");
        }
    } else {
        addDiagnosticMetrics(result, request, samples);
    }

    const auto preview_size = std::min<std::size_t>(50, samples.size());
    result.preview_before.assign(original.begin(), original.begin() + preview_size);
    result.preview_after.assign(samples.begin(), samples.begin() + preview_size);
    result.chart_before = sampledChart(original);
    result.chart_after = sampledChart(samples);
    return result;
}

} // namespace water::analysis
