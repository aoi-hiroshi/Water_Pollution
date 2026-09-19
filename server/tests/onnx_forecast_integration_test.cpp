#include "water/inference/onnx_forecaster.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2])) {
        std::cout << "[SKIP] supply an exported model and its golden JSON\n";
        return 77;
    }
    try {
        std::ifstream source(argv[2]);
        const auto golden = nlohmann::json::parse(source);
        const auto input = golden.at("input").get<std::vector<float>>();
        const auto expected = golden.at("expected").get<std::vector<std::vector<double>>>();
        if (input.size() != 1200 || expected.size() != 10) { throw std::runtime_error("invalid golden data"); }
        water::inference::OnnxForecaster model({.model_path=argv[1], .intra_op_threads=1});
        const auto result = model.forecast(input);
        double maximum_error=0.0;
        for (std::size_t step=0; step<10; ++step) {
            if (expected[step].size() != 4) { throw std::runtime_error("invalid golden target dimensions"); }
            for (std::size_t target=0; target<4; ++target) {
                const double error=std::abs(result.at(step)[target]-expected[step][target]);
                const double limit=golden.at("atol").get<double>()+golden.at("rtol").get<double>()*std::abs(expected[step][target]);
                if (!std::isfinite(result[step][target]) || error>limit) { throw std::runtime_error("C++ / PyTorch output mismatch"); }
                maximum_error=std::max(maximum_error,error);
            }
        }
        bool rejected=false;
        try { static_cast<void>(model.forecast(std::span<const float>(input.data(),10))); }
        catch (const water::inference::InferenceError&) { rejected=true; }
        if (!rejected) { throw std::runtime_error("invalid input length was accepted"); }
        std::cout << "[PASS] C++ ONNX Runtime real-model inference: " << model.modelName()
                  << " version " << model.modelVersion() << ", max abs error " << maximum_error << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
