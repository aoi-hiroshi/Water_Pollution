#pragma once

#include <stdexcept>

namespace water::inference {
class InferenceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class InferenceUnavailable : public InferenceError {
public:
    using InferenceError::InferenceError;
};
}  // namespace water::inference
