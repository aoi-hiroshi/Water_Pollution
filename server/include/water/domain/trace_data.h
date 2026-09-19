#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace water::domain {

struct TraceCandidate {
    std::size_t rank{0};
    std::int64_t class_label{0};
    std::int64_t company_id{0};
    std::string company_name;
    std::string company_code;
    double probability{0.0};
};

struct TraceResult {
    std::int64_t sample_id{0};
    std::string dataset;
    std::int64_t cleaning_run_id{0};
    std::string model_name;
    std::string model_version;
    TraceCandidate predicted;
    std::vector<TraceCandidate> candidates;
};

}  // namespace water::domain
