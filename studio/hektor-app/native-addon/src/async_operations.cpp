#include "async_operations.h"
#include "database.h"
#include "vdb/distance.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace hektor_native {

QueryVectorsWorker::QueryVectorsWorker(Napi::Function& callback, const std::vector<float>& queryVector, int topK)
    : Napi::AsyncWorker(callback), queryVector_(queryVector), topK_(std::max(1, topK)) {}

void QueryVectorsWorker::Execute() {
    if (auto db = DatabaseManager::get("active"); db && db->is_ready()) {
        vdb::QueryOptions opts;
        opts.k = static_cast<size_t>(topK_);
        auto query = db->query_vector(queryVector_, opts);
        if (!query.has_value()) {
            SetError(query.error().message);
            return;
        }
        results_.clear();
        results_.reserve(query->size());
        for (const auto& result : *query) {
            results_.emplace_back(std::to_string(result.id), result.distance);
        }
        return;
    }

    results_.clear();
    results_.reserve(static_cast<size_t>(topK_));
    for (int i = 0; i < topK_; ++i) {
        float distance = 0.0f;
        for (size_t j = i; j < queryVector_.size(); j += static_cast<size_t>(topK_)) {
            distance += std::fabs(queryVector_[j]);
        }
        results_.emplace_back("segment_" + std::to_string(i), distance / std::max<size_t>(1, queryVector_.size()));
    }
    std::sort(results_.begin(), results_.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
}

void QueryVectorsWorker::OnOK() {
    Napi::Env env = Env();
    Napi::Array results = Napi::Array::New(env, results_.size());

    for (size_t i = 0; i < results_.size(); i++) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("id", results_[i].first);
        result.Set("distance", results_[i].second);
        result.Set("score", 1.0f / (1.0f + results_[i].second));
        results.Set(static_cast<uint32_t>(i), result);
    }

    Callback().Call({env.Null(), results});
}

Napi::Value AsyncOperations::QueryVectorsAsync(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsArray() || !info[1].IsNumber() || !info[2].IsFunction()) {
        Napi::TypeError::New(env, "Expected (vector: Array, topK: Number, callback: Function)").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array vectorArray = info[0].As<Napi::Array>();
    std::vector<float> queryVector;
    queryVector.reserve(vectorArray.Length());
    for (uint32_t i = 0; i < vectorArray.Length(); i++) {
        queryVector.push_back(vectorArray.Get(i).As<Napi::Number>().FloatValue());
    }

    int topK = info[1].As<Napi::Number>().Int32Value();
    Napi::Function callback = info[2].As<Napi::Function>();

    auto* worker = new QueryVectorsWorker(callback, queryVector, topK);
    worker->Queue();
    return env.Undefined();
}

} // namespace hektor_native
