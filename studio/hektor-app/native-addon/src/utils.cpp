#include "utils.h"
#include "database.h"
#include "vdb/distance.hpp"
#include "vdb/core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <numeric>
#include <optional>
#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace hektor_native {
namespace {

std::vector<float> RequireVector(const Napi::Env& env, const Napi::Value& value, const char* name = "vector") {
    if (!value.IsArray()) {
        Napi::TypeError::New(env, std::string("Expected ") + name + " array").ThrowAsJavaScriptException();
        return {};
    }

    const auto arr = value.As<Napi::Array>();
    std::vector<float> vec;
    vec.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        if (!arr.Get(i).IsNumber()) {
            Napi::TypeError::New(env, std::string(name) + " entries must be numbers").ThrowAsJavaScriptException();
            return {};
        }
        vec.push_back(arr.Get(i).As<Napi::Number>().FloatValue());
    }
    return vec;
}

std::optional<std::vector<float>> TryVector(const Napi::Env& env, const Napi::Value& value, const char* name = "vector") {
    auto vec = RequireVector(env, value, name);
    if (env.IsExceptionPending()) {
        return std::nullopt;
    }
    return vec;
}

bool RequireSameSize(const Napi::Env& env, const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) {
        Napi::TypeError::New(env, "Vectors must have the same length").ThrowAsJavaScriptException();
        return false;
    }
    return true;
}

Napi::Array ToArray(Napi::Env env, const std::vector<float>& vec) {
    Napi::Array arr = Napi::Array::New(env, vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, vec[i]));
    }
    return arr;
}

std::optional<vdb::DistanceMetric> ParseMetric(const Napi::Env& env, const Napi::Value& value) {
    if (value.IsUndefined() || value.IsNull()) {
        return vdb::DistanceMetric::Cosine;
    }
    if (!value.IsString()) {
        Napi::TypeError::New(env, "Metric must be a string").ThrowAsJavaScriptException();
        return std::nullopt;
    }

    std::string metric = value.As<Napi::String>().Utf8Value();
    std::transform(metric.begin(), metric.end(), metric.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (metric == "cosine") return vdb::DistanceMetric::Cosine;
    if (metric == "euclidean" || metric == "l2") return vdb::DistanceMetric::L2;
    if (metric == "dot" || metric == "dot_product") return vdb::DistanceMetric::DotProduct;

    Napi::TypeError::New(env, "Unsupported metric").ThrowAsJavaScriptException();
    return std::nullopt;
}

std::string ToIsoString(std::chrono::system_clock::time_point tp) {
    const auto tt = std::chrono::system_clock::to_time_t(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

std::vector<std::string> ChunkTextInternal(std::string_view text, size_t chunk_size, size_t overlap) {
    std::vector<std::string> chunks;
    if (text.empty()) return chunks;
    if (chunk_size == 0) chunk_size = 512;
    if (overlap >= chunk_size) overlap = chunk_size > 1 ? chunk_size / 4 : 0;

    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = std::min(text.size(), pos + chunk_size);
        if (end < text.size()) {
            size_t boundary = text.rfind(' ', end);
            if (boundary != std::string_view::npos && boundary > pos) {
                end = boundary;
            }
        }
        if (end <= pos) end = std::min(text.size(), pos + chunk_size);
        chunks.emplace_back(text.substr(pos, end - pos));
        if (end >= text.size()) break;
        pos = end > overlap ? end - overlap : end;
    }
    return chunks;
}

uint64_t Fnv1a64(std::string_view value) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << value;
    return oss.str();
}

Napi::Array SearchResultsToArray(Napi::Env env, const vdb::SearchResults& results) {
    Napi::Array arr = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        Napi::Object item = Napi::Object::New(env);
        item.Set("id", Napi::Number::New(env, static_cast<double>(results[i].id)));
        item.Set("distance", Napi::Number::New(env, results[i].distance));
        item.Set("score", Napi::Number::New(env, results[i].score));
        arr.Set(static_cast<uint32_t>(i), item);
    }
    return arr;
}

} // namespace

void Utils::Init(Napi::Env env, Napi::Object& exports) {
    exports.Set("dotProduct", Napi::Function::New(env, DotProduct));
    exports.Set("euclideanDistance", Napi::Function::New(env, EuclideanDistance));
    exports.Set("squaredEuclidean", Napi::Function::New(env, SquaredEuclidean));
    exports.Set("cosineSimilarity", Napi::Function::New(env, CosineSimilarity));
    exports.Set("cosineDistance", Napi::Function::New(env, CosineDistance));
    exports.Set("computeDistance", Napi::Function::New(env, ComputeDistance));
    exports.Set("l2Distance", Napi::Function::New(env, L2Distance));
    exports.Set("l2SquaredDistance", Napi::Function::New(env, L2SquaredDistance));
    exports.Set("normalize", Napi::Function::New(env, Normalize));
    exports.Set("normalized", Napi::Function::New(env, Normalized));
    exports.Set("l2Norm", Napi::Function::New(env, L2Norm));
    exports.Set("vectorAdd", Napi::Function::New(env, Add));
    exports.Set("vectorSubtract", Napi::Function::New(env, Subtract));
    exports.Set("vectorScale", Napi::Function::New(env, Scale));
    exports.Set("vectorAddScaled", Napi::Function::New(env, AddScaled));
    exports.Set("vectorMultiply", Napi::Function::New(env, Multiply));
    exports.Set("vectorMean", Napi::Function::New(env, Mean));
    exports.Set("batchDistance", Napi::Function::New(env, BatchDistance));
    exports.Set("bruteForceKNN", Napi::Function::New(env, BruteForceKNN));
    exports.Set("nowTimestamp", Napi::Function::New(env, NowTimestamp));
    exports.Set("timestampToIso", Napi::Function::New(env, TimestampToIso));
    exports.Set("documentTypeName", Napi::Function::New(env, DocumentTypeName));
    exports.Set("detectBestDevice", Napi::Function::New(env, DetectBestDevice));
    exports.Set("deviceName", Napi::Function::New(env, DeviceName));
    exports.Set("formatToString", Napi::Function::New(env, FormatToString));
    exports.Set("parseMarkdown", Napi::Function::New(env, ParseMarkdown));
    exports.Set("detectDocumentType", Napi::Function::New(env, DetectDocumentType));
    exports.Set("extractDateFromFilename", Napi::Function::New(env, ExtractDateFromFilename));
    exports.Set("extractMarketData", Napi::Function::New(env, ExtractMarketData));
    exports.Set("chunkDocument", Napi::Function::New(env, ChunkDocument));
    exports.Set("chunkText", Napi::Function::New(env, ChunkText));
    exports.Set("contentHash", Napi::Function::New(env, ContentHash));
    exports.Set("fileHash", Napi::Function::New(env, FileHash));
    exports.Set("loadImage", Napi::Function::New(env, LoadImage));
    exports.Set("loadImageMemory", Napi::Function::New(env, LoadImageMemory));
    exports.Set("saveImage", Napi::Function::New(env, SaveImage));
    exports.Set("parseChartPath", Napi::Function::New(env, ParseChartPath));
    exports.Set("preprocessChart", Napi::Function::New(env, PreprocessChart));
    exports.Set("createGoldStandardDb", Napi::Function::New(env, CreateGoldStandardDb));
    exports.Set("openDatabase", Napi::Function::New(env, OpenDatabase));
    exports.Set("createLLMEngine", Napi::Function::New(env, CreateLLMEngine));
    exports.Set("detectSimdLevel", Napi::Function::New(env, DetectSimdLevel));
}

Napi::Value Utils::DotProduct(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::dot_product(vdb::VectorView(*a), vdb::VectorView(*b)));
}

Napi::Value Utils::EuclideanDistance(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::euclidean_distance(vdb::VectorView(*a), vdb::VectorView(*b)));
}

Napi::Value Utils::SquaredEuclidean(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::squared_euclidean(a->data(), b->data(), static_cast<vdb::Dim>(a->size())));
}

Napi::Value Utils::CosineSimilarity(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::cosine_similarity(vdb::VectorView(*a), vdb::VectorView(*b)));
}

Napi::Value Utils::CosineDistance(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::cosine_distance(vdb::VectorView(*a), vdb::VectorView(*b)));
}

Napi::Value Utils::ComputeDistance(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0], "first vector");
    auto b = TryVector(env, info[1], "second vector");
    auto metric = ParseMetric(env, info.Length() > 2 ? info[2] : env.Undefined());
    if (!a || !b || !metric || !RequireSameSize(env, *a, *b)) return env.Undefined();
    return Napi::Number::New(env, vdb::compute_distance(vdb::VectorView(*a), vdb::VectorView(*b), *metric));
}

Napi::Value Utils::L2Distance(const Napi::CallbackInfo& info) { return EuclideanDistance(info); }
Napi::Value Utils::L2SquaredDistance(const Napi::CallbackInfo& info) { return SquaredEuclidean(info); }

Napi::Value Utils::Normalize(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto vec = TryVector(env, info[0]);
    if (!vec) return env.Undefined();
    vdb::Vector owned(*vec);
    vdb::normalize(owned);
    return ToArray(env, std::vector<float>(owned.begin(), owned.end()));
}

Napi::Value Utils::Normalized(const Napi::CallbackInfo& info) { return Normalize(info); }

Napi::Value Utils::L2Norm(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto vec = TryVector(env, info[0]);
    if (!vec) return env.Undefined();
    return Napi::Number::New(env, vdb::l2_norm(vdb::VectorView(*vec)));
}

Napi::Value Utils::Add(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0]);
    auto b = TryVector(env, info[1]);
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    auto result = vdb::add(vdb::VectorView(*a), vdb::VectorView(*b));
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::Subtract(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0]);
    auto b = TryVector(env, info[1]);
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    auto result = vdb::subtract(vdb::VectorView(*a), vdb::VectorView(*b));
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::Scale(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto vec = TryVector(env, info[0]);
    if (!vec || info.Length() < 2 || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected vector and scalar").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto result = vdb::scale(vdb::VectorView(*vec), info[1].As<Napi::Number>().FloatValue());
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::AddScaled(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0]);
    auto b = TryVector(env, info[1]);
    if (!a || !b || !RequireSameSize(env, *a, *b) || info.Length() < 3 || !info[2].IsNumber()) {
        Napi::TypeError::New(env, "Expected two vectors and scale").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto result = vdb::add_scaled(vdb::VectorView(*a), vdb::VectorView(*b), info[2].As<Napi::Number>().FloatValue());
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::Multiply(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto a = TryVector(env, info[0]);
    auto b = TryVector(env, info[1]);
    if (!a || !b || !RequireSameSize(env, *a, *b)) return env.Undefined();
    auto result = vdb::multiply(vdb::VectorView(*a), vdb::VectorView(*b));
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::Mean(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected array of vectors").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Array input = info[0].As<Napi::Array>();
    std::vector<vdb::Vector> vectors;
    vectors.reserve(input.Length());
    for (uint32_t i = 0; i < input.Length(); ++i) {
        auto vec = TryVector(env, input.Get(i), "vector");
        if (!vec) return env.Undefined();
        vectors.emplace_back(*vec);
    }
    auto result = vdb::mean(std::span<const vdb::Vector>(vectors.data(), vectors.size()));
    return ToArray(env, std::vector<float>(result.begin(), result.end()));
}

Napi::Value Utils::BatchDistance(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto query = TryVector(env, info[0], "query vector");
    if (!query || info.Length() < 2 || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected query vector and array of target vectors").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto metric = ParseMetric(env, info.Length() > 2 ? info[2] : env.Undefined());
    if (!metric) return env.Undefined();

    Napi::Array targetArray = info[1].As<Napi::Array>();
    std::vector<vdb::Vector> targets;
    targets.reserve(targetArray.Length());
    for (uint32_t i = 0; i < targetArray.Length(); ++i) {
        auto vec = TryVector(env, targetArray.Get(i), "target vector");
        if (!vec) return env.Undefined();
        targets.emplace_back(*vec);
    }
    auto distances = vdb::batch_distance(vdb::VectorView(*query), std::span<const vdb::Vector>(targets.data(), targets.size()), *metric);
    Napi::Array result = Napi::Array::New(env, distances.size());
    for (size_t i = 0; i < distances.size(); ++i) {
        result.Set(static_cast<uint32_t>(i), Napi::Number::New(env, distances[i]));
    }
    return result;
}

Napi::Value Utils::BruteForceKNN(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto query = TryVector(env, info[0], "query vector");
    if (!query || info.Length() < 2 || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected query vector and array of target vectors").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    size_t k = info.Length() > 2 && info[2].IsNumber() ? info[2].As<Napi::Number>().Uint32Value() : 10;
    auto metric = ParseMetric(env, info.Length() > 3 ? info[3] : env.Undefined());
    if (!metric) return env.Undefined();

    Napi::Array targetArray = info[1].As<Napi::Array>();
    std::vector<vdb::Vector> targets;
    targets.reserve(targetArray.Length());
    for (uint32_t i = 0; i < targetArray.Length(); ++i) {
        auto vec = TryVector(env, targetArray.Get(i), "target vector");
        if (!vec) return env.Undefined();
        targets.emplace_back(*vec);
    }

    auto results = vdb::brute_force_knn(vdb::VectorView(*query), std::span<const vdb::Vector>(targets.data(), targets.size()), k, *metric);
    return SearchResultsToArray(env, results);
}

Napi::Value Utils::NowTimestamp(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return Napi::Number::New(env, static_cast<double>(ms));
}

Napi::Value Utils::TimestampToIso(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected timestamp in milliseconds").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto ms = static_cast<int64_t>(info[0].As<Napi::Number>().Int64Value());
    auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
    return Napi::String::New(env, ToIsoString(tp));
}

Napi::Value Utils::DocumentTypeName(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1) return Napi::String::New(env, "unknown");
    if (info[0].IsString()) return info[0];
    if (!info[0].IsNumber()) return Napi::String::New(env, "unknown");
    return DocumentTypeToNapi(static_cast<vdb::DocumentType>(info[0].As<Napi::Number>().Uint32Value()), env);
}

Napi::Value Utils::DetectBestDevice(const Napi::CallbackInfo& info) {
    auto env = info.Env();
#if defined(VDB_USE_CUDA)
    return Napi::String::New(env, "cuda");
#elif defined(_WIN32)
    return Napi::String::New(env, "directml");
#else
    return Napi::String::New(env, "cpu");
#endif
}

Napi::Value Utils::DeviceName(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    std::string device = "cpu";
    if (info.Length() > 0 && info[0].IsString()) device = info[0].As<Napi::String>().Utf8Value();
    if (device == "cuda") return Napi::String::New(env, "NVIDIA CUDA");
    if (device == "directml") return Napi::String::New(env, "DirectML GPU");
    return Napi::String::New(env, "CPU");
}

Napi::Value Utils::FormatToString(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1) return Napi::String::New(env, "unknown");
    if (info[0].IsString()) return info[0];
    if (!info[0].IsNumber()) return Napi::String::New(env, "unknown");
    switch (info[0].As<Napi::Number>().Int32Value()) {
        case 0: return Napi::String::New(env, "jsonl");
        case 1: return Napi::String::New(env, "tfrecord");
        case 2: return Napi::String::New(env, "pytorch_dataset");
        case 3: return Napi::String::New(env, "huggingface");
        case 4: return Napi::String::New(env, "parquet");
        default: return Napi::String::New(env, "unknown");
    }
}

Napi::Value Utils::ParseMarkdown(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected markdown text").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string markdown = info[0].As<Napi::String>().Utf8Value();
    std::istringstream stream(markdown);
    std::string line;
    Napi::Array headings = Napi::Array::New(env);
    Napi::Array bullets = Napi::Array::New(env);
    std::vector<std::string> paragraphs;
    uint32_t h = 0;
    uint32_t b = 0;
    std::string paragraph;

    while (std::getline(stream, line)) {
        if (!line.empty() && line[0] == '#') {
            if (!paragraph.empty()) {
                paragraphs.push_back(paragraph);
                paragraph.clear();
            }
            size_t level = line.find_first_not_of('#');
            Napi::Object item = Napi::Object::New(env);
            item.Set("level", Napi::Number::New(env, static_cast<double>(level)));
            item.Set("text", Napi::String::New(env, line.substr(level == std::string::npos ? 0 : level)));
            headings.Set(h++, item);
        } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
            bullets.Set(b++, Napi::String::New(env, line.substr(2)));
        } else if (line.empty()) {
            if (!paragraph.empty()) {
                paragraphs.push_back(paragraph);
                paragraph.clear();
            }
        } else {
            if (!paragraph.empty()) paragraph += '\n';
            paragraph += line;
        }
    }
    if (!paragraph.empty()) paragraphs.push_back(paragraph);

    Napi::Array paras = Napi::Array::New(env, paragraphs.size());
    for (size_t i = 0; i < paragraphs.size(); ++i) paras.Set(static_cast<uint32_t>(i), Napi::String::New(env, paragraphs[i]));

    Napi::Object result = Napi::Object::New(env);
    result.Set("headings", headings);
    result.Set("bullets", bullets);
    result.Set("paragraphs", paras);
    result.Set("wordCount", Napi::Number::New(env, std::count_if(markdown.begin(), markdown.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }) + (markdown.empty() ? 0 : 1)));
    return result;
}

Napi::Value Utils::DetectDocumentType(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected text or filename").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string text = info[0].As<Napi::String>().Utf8Value();
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("premarket") != std::string::npos || lower.find("pre-market") != std::string::npos) return Napi::String::New(env, "pre_market");
    if (lower.find("weekly") != std::string::npos) return Napi::String::New(env, "weekly_rundown");
    if (lower.find("monthly") != std::string::npos) return Napi::String::New(env, "monthly_report");
    if (lower.find("calendar") != std::string::npos) return Napi::String::New(env, "economic_calendar");
    if (lower.find("chart") != std::string::npos || lower.ends_with(".png") || lower.ends_with(".jpg")) return Napi::String::New(env, "chart");
    return Napi::String::New(env, "journal");
}

Napi::Value Utils::ExtractDateFromFilename(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected path string").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    const std::string path = info[0].As<Napi::String>().Utf8Value();
    std::regex rx(R"((\d{4})[-_](\d{2})[-_](\d{2}))");
    std::smatch match;
    if (std::regex_search(path, match, rx)) {
        return Napi::String::New(env, match[1].str() + "-" + match[2].str() + "-" + match[3].str());
    }
    return env.Null();
}

Napi::Value Utils::ExtractMarketData(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected content string").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::string text = info[0].As<Napi::String>().Utf8Value();
    Napi::Object result = Napi::Object::New(env);
    const std::vector<std::pair<std::string, std::string>> patterns = {
        {"gold", R"((gold|xau)[^0-9]{0,12}(\d+(?:\.\d+)?))"},
        {"silver", R"((silver|xag)[^0-9]{0,12}(\d+(?:\.\d+)?))"},
        {"dxy", R"((dxy)[^0-9]{0,12}(\d+(?:\.\d+)?))"},
        {"vix", R"((vix)[^0-9]{0,12}(\d+(?:\.\d+)?))"},
        {"yield10y", R"((10y|yield)[^0-9]{0,12}(\d+(?:\.\d+)?))"}
    };
    for (const auto& [key, pattern] : patterns) {
        std::regex rx(pattern, std::regex::icase);
        std::smatch match;
        if (std::regex_search(text, match, rx)) {
            result.Set(key, Napi::Number::New(env, std::stod(match[2].str())));
        }
    }
    return result;
}

Napi::Value Utils::ChunkDocument(const Napi::CallbackInfo& info) { return ChunkText(info); }

Napi::Value Utils::ChunkText(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected text string").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    size_t chunkSize = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 512;
    size_t overlap = info.Length() > 2 && info[2].IsNumber() ? info[2].As<Napi::Number>().Uint32Value() : 50;
    auto chunks = ChunkTextInternal(info[0].As<Napi::String>().Utf8Value(), chunkSize, overlap);
    Napi::Array arr = Napi::Array::New(env, chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) arr.Set(static_cast<uint32_t>(i), Napi::String::New(env, chunks[i]));
    return arr;
}

Napi::Value Utils::ContentHash(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected content string").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return Napi::String::New(env, Hex64(Fnv1a64(info[0].As<Napi::String>().Utf8Value())));
}

Napi::Value Utils::FileHash(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected file path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::ifstream file(info[0].As<Napi::String>().Utf8Value(), std::ios::binary);
    if (!file) {
        Napi::Error::New(env, "Failed to open file").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return Napi::String::New(env, Hex64(Fnv1a64(buffer.str())));
}

Napi::Value Utils::LoadImage(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected image path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    fs::path path = info[0].As<Napi::String>().Utf8Value();
    if (!fs::exists(path)) {
        Napi::Error::New(env, "Image file not found").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Object result = Napi::Object::New(env);
    result.Set("path", Napi::String::New(env, path.string()));
    result.Set("format", Napi::String::New(env, path.extension().string()));
    result.Set("sizeBytes", Napi::Number::New(env, static_cast<double>(fs::file_size(path))));
    return result;
}

Napi::Value Utils::LoadImageMemory(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !(info[0].IsBuffer() || info[0].IsTypedArray())) {
        Napi::TypeError::New(env, "Expected image buffer").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    size_t size = 0;
    const uint8_t* data = nullptr;
    if (info[0].IsBuffer()) {
        auto buffer = info[0].As<Napi::Buffer<uint8_t>>();
        size = buffer.Length();
        data = buffer.Data();
    } else {
        auto typed = info[0].As<Napi::Uint8Array>();
        size = typed.ElementLength();
        data = typed.Data();
    }
    Napi::Object result = Napi::Object::New(env);
    result.Set("sizeBytes", Napi::Number::New(env, static_cast<double>(size)));
    result.Set("contentHash", Napi::String::New(env, Hex64(Fnv1a64(std::string_view(reinterpret_cast<const char*>(data), size)))));
    return result;
}

Napi::Value Utils::SaveImage(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 2 || !info[0].IsString() || !(info[1].IsBuffer() || info[1].IsTypedArray())) {
        Napi::TypeError::New(env, "Expected output path and image buffer").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    const std::string path = info[0].As<Napi::String>().Utf8Value();
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (info[1].IsBuffer()) {
        auto buffer = info[1].As<Napi::Buffer<uint8_t>>();
        data = buffer.Data();
        size = buffer.Length();
    } else {
        auto typed = info[1].As<Napi::Uint8Array>();
        data = typed.Data();
        size = typed.ElementLength();
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        Napi::Error::New(env, "Failed to create output file").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    return Napi::Boolean::New(env, out.good());
}

Napi::Value Utils::ParseChartPath(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected chart path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    fs::path path = info[0].As<Napi::String>().Utf8Value();
    Napi::Object result = Napi::Object::New(env);
    result.Set("path", Napi::String::New(env, path.string()));
    result.Set("asset", Napi::String::New(env, path.stem().string()));
    result.Set("timeframe", Napi::String::New(env, path.parent_path().filename().string().find("week") != std::string::npos ? "weekly" : "daily"));
    std::regex rx(R"((\d{4}[-_]\d{2}[-_]\d{2}))");
    std::smatch match;
    std::string full = path.string();
    if (std::regex_search(full, match, rx)) {
        std::string normalized = match[1].str();
        std::replace(normalized.begin(), normalized.end(), '_', '-');
        result.Set("date", Napi::String::New(env, normalized));
    }
    return result;
}

Napi::Value Utils::PreprocessChart(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected chart path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    fs::path path = info[0].As<Napi::String>().Utf8Value();
    Napi::Object result = Napi::Object::New(env);
    result.Set("path", Napi::String::New(env, path.string()));
    result.Set("exists", Napi::Boolean::New(env, fs::exists(path)));
    result.Set("suggestedBackground", Napi::String::New(env, "dark"));
    result.Set("normalizedName", Napi::String::New(env, path.stem().string()));
    return result;
}

Napi::Value Utils::CreateGoldStandardDb(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected database path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    vdb::DatabaseConfig config;
    config.path = info[0].As<Napi::String>().Utf8Value();
    auto db = std::make_shared<vdb::VectorDatabase>(config);
    auto init = db->init();
    if (!init) {
        Napi::Error::New(env, init.error().message).ThrowAsJavaScriptException();
        return env.Undefined();
    }
    DatabaseManager::set("active", db);
    Napi::Object result = Napi::Object::New(env);
    result.Set("dbId", Napi::String::New(env, "active"));
    result.Set("path", Napi::String::New(env, config.path.string()));
    result.Set("dimension", Napi::Number::New(env, config.dimension));
    return result;
}

Napi::Value Utils::OpenDatabase(const Napi::CallbackInfo& info) { return CreateGoldStandardDb(info); }

Napi::Value Utils::CreateLLMEngine(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    Napi::Object result = Napi::Object::New(env);
    result.Set("available", Napi::Boolean::New(env, true));
    result.Set("backend", Napi::String::New(env, "llm_engine"));
    result.Set("supportsLocalModels", Napi::Boolean::New(env, true));
    return result;
}

Napi::Value Utils::DetectSimdLevel(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    switch (vdb::SIMD_LEVEL) {
        case vdb::SimdLevel::AVX512: return Napi::String::New(env, "avx512");
        case vdb::SimdLevel::AVX2: return Napi::String::New(env, "avx2");
        case vdb::SimdLevel::SSE4: return Napi::String::New(env, "sse4");
        default: return Napi::String::New(env, "scalar");
    }
}

} // namespace hektor_native
