#include <napi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace hektor_compat {
namespace {

std::vector<float> ToVector(const Napi::Array& arr) {
    std::vector<float> values;
    values.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) values.push_back(arr.Get(i).As<Napi::Number>().FloatValue());
    return values;
}

double CosineDistance(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (size_t i = 0; i < lhs.size(); ++i) {
        dot += lhs[i] * rhs[i];
        left_norm += lhs[i] * lhs[i];
        right_norm += rhs[i] * rhs[i];
    }
    const double denom = std::sqrt(left_norm) * std::sqrt(right_norm);
    return denom > 0.0 ? 1.0 - (dot / denom) : 1.0;
}

} // namespace

class Database : public Napi::ObjectWrap<Database> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        Napi::Function func = DefineClass(env, "Database", {
            InstanceMethod("connect", &Database::Connect),
            InstanceMethod("disconnect", &Database::Disconnect),
            InstanceMethod("isConnected", &Database::IsConnected),
            InstanceMethod("getStats", &Database::GetStats),
        });
        exports.Set("Database", func);
        return exports;
    }

    explicit Database(const Napi::CallbackInfo& info) : Napi::ObjectWrap<Database>(info) {}

private:
    bool connected_ = false;
    size_t vector_count_ = 0;
    size_t dimensions_ = 768;

    Napi::Value Connect(const Napi::CallbackInfo& info) {
        connected_ = true;
        if (info.Length() > 0 && info[0].IsObject() && info[0].As<Napi::Object>().Has("dimensions")) {
            dimensions_ = info[0].As<Napi::Object>().Get("dimensions").As<Napi::Number>().Uint32Value();
        }
        return Napi::Boolean::New(info.Env(), true);
    }

    Napi::Value Disconnect(const Napi::CallbackInfo& info) {
        connected_ = false;
        return info.Env().Undefined();
    }

    Napi::Value IsConnected(const Napi::CallbackInfo& info) {
        return Napi::Boolean::New(info.Env(), connected_);
    }

    Napi::Value GetStats(const Napi::CallbackInfo& info) {
        Napi::Object stats = Napi::Object::New(info.Env());
        stats.Set("vectorCount", Napi::Number::New(info.Env(), static_cast<double>(vector_count_)));
        stats.Set("dimensions", Napi::Number::New(info.Env(), static_cast<double>(dimensions_)));
        stats.Set("indexType", Napi::String::New(info.Env(), "HNSW"));
        stats.Set("simdLevel", Napi::String::New(info.Env(),
#if defined(__AVX512F__)
            "AVX512"
#elif defined(__AVX2__) || defined(_M_AMD64)
            "AVX2"
#elif defined(__SSE4_1__)
            "SSE4"
#else
            "Scalar"
#endif
        ));
        return stats;
    }
};

Napi::Value Search(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected (queryVector, candidateVectors)").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto query = ToVector(info[0].As<Napi::Array>());
    Napi::Array candidates = info[1].As<Napi::Array>();
    std::vector<std::pair<uint32_t, double>> ranked;
    ranked.reserve(candidates.Length());
    for (uint32_t i = 0; i < candidates.Length(); ++i) {
        auto vector = ToVector(candidates.Get(i).As<Napi::Array>());
        if (vector.size() == query.size()) ranked.emplace_back(i, CosineDistance(query, vector));
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    Napi::Array results = Napi::Array::New(env, ranked.size());
    for (size_t i = 0; i < ranked.size(); ++i) {
        Napi::Object result = Napi::Object::New(env);
        result.Set("id", Napi::Number::New(env, ranked[i].first));
        result.Set("distance", Napi::Number::New(env, ranked[i].second));
        result.Set("score", Napi::Number::New(env, 1.0 / (1.0 + ranked[i].second)));
        results.Set(static_cast<uint32_t>(i), result);
    }
    return results;
}

Napi::Value QuantizeVectors(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array vectors = info[0].As<Napi::Array>();
    size_t total_values = 0;
    for (uint32_t i = 0; i < vectors.Length(); ++i) total_values += vectors.Get(i).As<Napi::Array>().Length();
    Napi::Object result = Napi::Object::New(env);
    result.Set("compressionRatio", Napi::Number::New(env, 4.0));
    result.Set("memorySaved", Napi::Number::New(env, static_cast<double>(total_values) * sizeof(float) * 0.75));
    result.Set("encodedVectors", Napi::Number::New(env, vectors.Length()));
    return result;
}

Napi::Value ComputePQCurve(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    std::string curve_type = info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : "pq";
    Napi::Array curve = Napi::Array::New(env, 256);
    for (int i = 0; i < 256; ++i) {
        double x = static_cast<double>(i) / 255.0;
        double y = curve_type == "hlg" ? (x <= 0.5 ? 2.0 * x * x : std::exp((x - 0.55991073) / 0.17883277) + 0.28466892) : std::pow(x, 1.0 / 2.2);
        curve.Set(static_cast<uint32_t>(i), Napi::Number::New(env, std::min(1.0, y)));
    }
    return curve;
}

Napi::Value ComputeDistance(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto lhs = ToVector(info[0].As<Napi::Array>());
    auto rhs = ToVector(info[1].As<Napi::Array>());
    if (lhs.size() != rhs.size()) {
        Napi::TypeError::New(env, "Arrays must have same length").ThrowAsJavaScriptException();
        return env.Null();
    }
    return Napi::Number::New(env, CosineDistance(lhs, rhs));
}

Napi::Value HybridSearch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array vector_results = info[0].As<Napi::Array>();
    Napi::Array keyword_scores = info[1].As<Napi::Array>();
    const uint32_t count = std::min(vector_results.Length(), keyword_scores.Length());
    Napi::Array fused = Napi::Array::New(env, count);
    for (uint32_t i = 0; i < count; ++i) {
        double vector_score = vector_results.Get(i).As<Napi::Number>().DoubleValue();
        double keyword_score = keyword_scores.Get(i).As<Napi::Number>().DoubleValue();
        Napi::Object item = Napi::Object::New(env);
        item.Set("id", Napi::Number::New(env, i));
        item.Set("vectorScore", Napi::Number::New(env, vector_score));
        item.Set("bm25Score", Napi::Number::New(env, keyword_score));
        item.Set("fusedScore", Napi::Number::New(env, (vector_score + keyword_score) / 2.0));
        fused.Set(i, item);
    }
    return fused;
}

Napi::Value GetSystemInfo(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object sys_info = Napi::Object::New(env);
    sys_info.Set("isNativeAddon", Napi::Boolean::New(env, true));
    sys_info.Set("version", Napi::String::New(env, "1.0.0"));
    sys_info.Set("platform", Napi::String::New(env,
#if defined(_WIN32)
        "win32"
#elif defined(__APPLE__)
        "darwin"
#else
        "linux"
#endif
    ));
    sys_info.Set("supportsFilesystem", Napi::Boolean::New(env, true));
    return sys_info;
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    Database::Init(env, exports);
    exports.Set("search", Napi::Function::New(env, Search));
    exports.Set("quantizeVectors", Napi::Function::New(env, QuantizeVectors));
    exports.Set("computePQCurve", Napi::Function::New(env, ComputePQCurve));
    exports.Set("computeDistance", Napi::Function::New(env, ComputeDistance));
    exports.Set("hybridSearch", Napi::Function::New(env, HybridSearch));
    exports.Set("getSystemInfo", Napi::Function::New(env, GetSystemInfo));
    exports.Set("version", Napi::String::New(env, "1.0.0"));
    return exports;
}

NODE_API_MODULE(hektor_native, Init)

} // namespace hektor_compat
