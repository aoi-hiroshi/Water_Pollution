#include "water/inference/onnx_trace_classifier.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

int main(int argc,char** argv) {
    if(argc!=3 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2])) {
        std::cout<<"[SKIP] supply model and golden JSON\n";return 77;
    }
    try {
        std::ifstream source(argv[2]);const auto golden=nlohmann::json::parse(source);
        water::inference::OnnxTraceClassifier model({.model_path=argv[1]});
        double maximum_error=0;std::size_t count=0;
        for(const auto& input:golden.at("input")) {
            const auto values=input.get<std::vector<float>>();
            if(values.size()!=10) throw std::runtime_error("invalid golden features");
            water::inference::TraceFeatureVector features;std::copy(values.begin(),values.end(),features.begin());
            const auto result=model.classify(features);
            if(result.predicted_label!=golden.at("labels")[count].get<std::int64_t>())
                throw std::runtime_error("business-label mapping mismatch");
            for(std::size_t i=0;i<6;++i) {
                const double expected=golden.at("probabilities")[count][i].get<double>();
                const double error=std::abs(result.probabilities.at(i).probability-expected);
                if(error>golden.at("atol").get<double>()+golden.at("rtol").get<double>()*std::abs(expected))
                    throw std::runtime_error("C++ ONNX/sklearn probability mismatch");
                maximum_error=std::max(maximum_error,error);
            } ++count;
        }
        if(count==0) throw std::runtime_error("empty golden fixture");
        water::inference::TraceFeatureVector invalid{};invalid[0]=std::numeric_limits<float>::quiet_NaN();
        bool rejected=false;try{(void)model.classify(invalid);}catch(const water::inference::InferenceError&){rejected=true;}
        if(!rejected) throw std::runtime_error("non-finite input accepted");
        std::cout<<"[PASS] C++ real classification ONNX: "<<count<<" samples, max probability error "<<maximum_error<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
