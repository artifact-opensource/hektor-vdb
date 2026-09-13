#include "index.h"

#include "database.h"
#include "vdb/index.hpp"
#include "vdb/index/metadata_index.hpp"

#include <algorithm>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hektor_native {
namespace {

Napi::FunctionReference g_hnsw_constructor;
Napi::FunctionReference g_flat_constructor;
Napi::FunctionReference g_metadata_constructor;

Napi::Object SearchResultToObject(Napi::Env env, const vdb::SearchResult& result) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("id", Napi::Number::New(env, static_cast<double>(result.id)));
    obj.Set("distance", Napi::Number::New(env, result.distance));
    obj.Set("score", Napi::Number::New(env, result.score));
    return obj;
}

Napi::Array QueryResultsToArray(Napi::Env env, const vdb::SearchResults& results) {
    Napi::Array out = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        out.Set(static_cast<uint32_t>(i), SearchResultToObject(env, results[i]));
    }
    return out;
}

Napi::Object IndexStatsToObject(Napi::Env env, const vdb::IndexStats& stats) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("totalVectors", Napi::Number::New(env, static_cast<double>(stats.total_vectors)));
    obj.Set("dimension", Napi::Number::New(env, stats.dimension));
    obj.Set("memoryUsageBytes", Napi::Number::New(env, static_cast<double>(stats.memory_usage_bytes)));
    obj.Set("indexSizeBytes", Napi::Number::New(env, static_cast<double>(stats.index_size_bytes)));
    obj.Set("metric", Napi::Number::New(env, static_cast<uint32_t>(stats.metric)));
    obj.Set("indexType", Napi::String::New(env, stats.index_type));
    return obj;
}

vdb::HnswConfig ParseHnswConfig(const Napi::Value& value) {
    vdb::HnswConfig config;
    if (!value.IsObject()) {
        return config;
    }
    Napi::Object obj = value.As<Napi::Object>();
    if (obj.Has("dimension")) config.dimension = obj.Get("dimension").As<Napi::Number>().Uint32Value();
    if (obj.Has("maxElements")) config.max_elements = obj.Get("maxElements").As<Napi::Number>().Int64Value();
    if (obj.Has("M")) config.M = obj.Get("M").As<Napi::Number>().Int64Value();
    if (obj.Has("efConstruction")) config.ef_construction = obj.Get("efConstruction").As<Napi::Number>().Int64Value();
    if (obj.Has("efSearch")) config.ef_search = obj.Get("efSearch").As<Napi::Number>().Int64Value();
    if (obj.Has("metric")) config.metric = static_cast<vdb::DistanceMetric>(obj.Get("metric").As<Napi::Number>().Uint32Value());
    if (obj.Has("seed")) config.seed = obj.Get("seed").As<Napi::Number>().Int64Value();
    if (obj.Has("allowReplace")) config.allow_replace = obj.Get("allowReplace").As<Napi::Boolean>().Value();
    if (obj.Has("numThreads")) config.num_threads = obj.Get("numThreads").As<Napi::Number>().Int64Value();
    return config;
}

Napi::Object HnswConfigToObject(Napi::Env env, const vdb::HnswConfig& config) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("dimension", Napi::Number::New(env, config.dimension));
    obj.Set("maxElements", Napi::Number::New(env, static_cast<double>(config.max_elements)));
    obj.Set("M", Napi::Number::New(env, static_cast<double>(config.M)));
    obj.Set("efConstruction", Napi::Number::New(env, static_cast<double>(config.ef_construction)));
    obj.Set("efSearch", Napi::Number::New(env, static_cast<double>(config.ef_search)));
    obj.Set("metric", Napi::Number::New(env, static_cast<uint32_t>(config.metric)));
    obj.Set("seed", Napi::Number::New(env, static_cast<double>(config.seed)));
    obj.Set("allowReplace", Napi::Boolean::New(env, config.allow_replace));
    obj.Set("numThreads", Napi::Number::New(env, static_cast<double>(config.num_threads)));
    return obj;
}

vdb::index::FilterCondition ParseFilterCondition(const Napi::Object& obj) {
    vdb::index::FilterCondition cond;
    cond.field = obj.Get("field").As<Napi::String>().Utf8Value();
    const auto op = obj.Has("op") ? obj.Get("op").As<Napi::String>().Utf8Value() : std::string("eq");
    if (op == "ne") cond.op = vdb::index::FilterOp::NotEqual;
    else if (op == "lt") cond.op = vdb::index::FilterOp::LessThan;
    else if (op == "lte") cond.op = vdb::index::FilterOp::LessOrEqual;
    else if (op == "gt") cond.op = vdb::index::FilterOp::GreaterThan;
    else if (op == "gte") cond.op = vdb::index::FilterOp::GreaterOrEqual;
    else if (op == "range") cond.op = vdb::index::FilterOp::Range;
    else if (op == "in") cond.op = vdb::index::FilterOp::In;
    else if (op == "nin") cond.op = vdb::index::FilterOp::NotIn;
    else if (op == "contains") cond.op = vdb::index::FilterOp::Contains;
    else if (op == "startsWith") cond.op = vdb::index::FilterOp::StartsWith;
    else cond.op = vdb::index::FilterOp::Equal;

    if (obj.Has("value")) cond.value = obj.Get("value").ToString().Utf8Value();
    if (obj.Has("value2")) cond.value2 = obj.Get("value2").ToString().Utf8Value();
    if (obj.Has("values") && obj.Get("values").IsArray()) {
        Napi::Array values = obj.Get("values").As<Napi::Array>();
        cond.values.reserve(values.Length());
        for (uint32_t i = 0; i < values.Length(); ++i) {
            cond.values.push_back(values.Get(i).ToString().Utf8Value());
        }
    }
    return cond;
}

Napi::Array SetToArray(Napi::Env env, const std::set<vdb::VectorId>& values) {
    Napi::Array arr = Napi::Array::New(env, values.size());
    uint32_t index = 0;
    for (auto id : values) {
        arr.Set(index++, Napi::Number::New(env, static_cast<double>(id)));
    }
    return arr;
}

} // namespace

Napi::Object HnswIndexWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "HnswIndex", {
        InstanceMethod("add", &HnswIndexWrap::Add),
        InstanceMethod("addBatch", &HnswIndexWrap::AddBatch),
        InstanceMethod("search", &HnswIndexWrap::Search),
        InstanceMethod("searchFiltered", &HnswIndexWrap::SearchFiltered),
        InstanceMethod("remove", &HnswIndexWrap::Remove),
        InstanceMethod("contains", &HnswIndexWrap::Contains),
        InstanceMethod("getVector", &HnswIndexWrap::GetVector),
        InstanceMethod("size", &HnswIndexWrap::Size),
        InstanceMethod("capacity", &HnswIndexWrap::Capacity),
        InstanceMethod("empty", &HnswIndexWrap::Empty),
        InstanceMethod("dimension", &HnswIndexWrap::Dimension),
        InstanceMethod("config", &HnswIndexWrap::Config),
        InstanceMethod("stats", &HnswIndexWrap::Stats),
        InstanceMethod("setEfSearch", &HnswIndexWrap::SetEfSearch),
        InstanceMethod("resize", &HnswIndexWrap::Resize),
        InstanceMethod("optimize", &HnswIndexWrap::Optimize),
        InstanceMethod("save", &HnswIndexWrap::Save),
        InstanceMethod("serialize", &HnswIndexWrap::Serialize),
        StaticMethod("load", &HnswIndexWrap::Load),
        StaticMethod("deserialize", &HnswIndexWrap::Deserialize),
    });
    g_hnsw_constructor = Napi::Persistent(func);
    g_hnsw_constructor.SuppressDestruct();
    exports.Set("HnswIndex", func);
    return exports;
}

HnswIndexWrap::HnswIndexWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<HnswIndexWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::HnswIndex>>>();
        index_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    index_ = std::make_unique<vdb::HnswIndex>(ParseHnswConfig(info.Length() > 0 ? info[0] : info.Env().Undefined()));
}

Napi::Value HnswIndexWrap::Add(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected (id, vector)").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto result = index_->add(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()), JSToVector(info[1].As<Napi::Array>()));
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value HnswIndexWrap::AddBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsArray()) {
        Napi::TypeError::New(env, "Expected (ids, vectors)").ThrowAsJavaScriptException();
        return env.Null();
    }
    Napi::Array ids_array = info[0].As<Napi::Array>();
    Napi::Array vectors_array = info[1].As<Napi::Array>();
    if (ids_array.Length() != vectors_array.Length()) {
        Napi::TypeError::New(env, "ids and vectors must have matching lengths").ThrowAsJavaScriptException();
        return env.Null();
    }
    std::vector<vdb::VectorId> ids;
    std::vector<vdb::Vector> vectors;
    ids.reserve(ids_array.Length());
    vectors.reserve(vectors_array.Length());
    for (uint32_t i = 0; i < ids_array.Length(); ++i) {
        ids.push_back(static_cast<vdb::VectorId>(ids_array.Get(i).As<Napi::Number>().Int64Value()));
        vectors.emplace_back(JSToVector(vectors_array.Get(i).As<Napi::Array>()));
    }
    auto result = index_->add_batch(std::span<const vdb::VectorId>(ids.data(), ids.size()), std::span<const vdb::Vector>(vectors.data(), vectors.size()));
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value HnswIndexWrap::Search(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 2 || !info[0].IsArray() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected (query, k)").ThrowAsJavaScriptException();
        return env.Null();
    }
    return QueryResultsToArray(env, index_->search(JSToVector(info[0].As<Napi::Array>()), info[1].As<Napi::Number>().Uint32Value()));
}

Napi::Value HnswIndexWrap::SearchFiltered(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3 || !info[0].IsArray() || !info[1].IsNumber() || !info[2].IsFunction()) {
        Napi::TypeError::New(env, "Expected (query, k, predicate)").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto query = JSToVector(info[0].As<Napi::Array>());
    auto k = info[1].As<Napi::Number>().Uint32Value();
    Napi::Function predicate = info[2].As<Napi::Function>();
    auto results = index_->search_filtered(query, k, [&](vdb::VectorId id) {
        auto value = predicate.Call({Napi::Number::New(env, static_cast<double>(id))});
        return value.IsBoolean() && value.As<Napi::Boolean>().Value();
    });
    return QueryResultsToArray(env, results);
}

Napi::Value HnswIndexWrap::Remove(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto result = index_->remove(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()));
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value HnswIndexWrap::Contains(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), index_->contains(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())));
}

Napi::Value HnswIndexWrap::GetVector(const Napi::CallbackInfo& info) {
    auto result = index_->get_vector(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()));
    return result ? VectorToJS(info.Env(), std::vector<float>(result->begin(), result->end())) : info.Env().Null();
}

Napi::Value HnswIndexWrap::Size(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(index_->size())); }
Napi::Value HnswIndexWrap::Capacity(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(index_->capacity())); }
Napi::Value HnswIndexWrap::Empty(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), index_->empty()); }
Napi::Value HnswIndexWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), index_->dimension()); }
Napi::Value HnswIndexWrap::Config(const Napi::CallbackInfo& info) { return HnswConfigToObject(info.Env(), index_->config()); }
Napi::Value HnswIndexWrap::Stats(const Napi::CallbackInfo& info) { return IndexStatsToObject(info.Env(), index_->stats()); }
Napi::Value HnswIndexWrap::SetEfSearch(const Napi::CallbackInfo& info) { index_->set_ef_search(info[0].As<Napi::Number>().Uint32Value()); return info.Env().Undefined(); }

Napi::Value HnswIndexWrap::Resize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto result = index_->resize(info[0].As<Napi::Number>().Int64Value());
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value HnswIndexWrap::Optimize(const Napi::CallbackInfo& info) {
    index_->optimize();
    return info.Env().Undefined();
}

Napi::Value HnswIndexWrap::Save(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto result = index_->save(info[0].As<Napi::String>().Utf8Value());
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value HnswIndexWrap::Load(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto loaded = vdb::HnswIndex::load(info[0].As<Napi::String>().Utf8Value());
    auto index = UnwrapResult(loaded, env);
    if (env.IsExceptionPending()) return env.Null();
    auto* holder = new std::unique_ptr<vdb::HnswIndex>(std::make_unique<vdb::HnswIndex>(std::move(index)));
    return g_hnsw_constructor.New({Napi::External<std::unique_ptr<vdb::HnswIndex>>::New(env, holder)});
}

Napi::Value HnswIndexWrap::Serialize(const Napi::CallbackInfo& info) {
    const auto data = index_->serialize();
    return Napi::Buffer<uint8_t>::Copy(info.Env(), data.data(), data.size());
}

Napi::Value HnswIndexWrap::Deserialize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!info[0].IsBuffer()) {
        Napi::TypeError::New(env, "Expected Buffer").ThrowAsJavaScriptException();
        return env.Null();
    }
    auto buffer = info[0].As<Napi::Buffer<uint8_t>>();
    auto loaded = vdb::HnswIndex::deserialize(std::span<const uint8_t>(buffer.Data(), buffer.Length()));
    auto index = UnwrapResult(loaded, env);
    if (env.IsExceptionPending()) return env.Null();
    auto* holder = new std::unique_ptr<vdb::HnswIndex>(std::make_unique<vdb::HnswIndex>(std::move(index)));
    return g_hnsw_constructor.New({Napi::External<std::unique_ptr<vdb::HnswIndex>>::New(env, holder)});
}

Napi::Object FlatIndexWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "FlatIndex", {
        InstanceMethod("add", &FlatIndexWrap::Add),
        InstanceMethod("search", &FlatIndexWrap::Search),
        InstanceMethod("contains", &FlatIndexWrap::Contains),
        InstanceMethod("getVector", &FlatIndexWrap::GetVector),
        InstanceMethod("size", &FlatIndexWrap::Size),
        InstanceMethod("dimension", &FlatIndexWrap::Dimension),
        InstanceMethod("save", &FlatIndexWrap::Save),
        StaticMethod("load", &FlatIndexWrap::Load),
    });
    g_flat_constructor = Napi::Persistent(func);
    g_flat_constructor.SuppressDestruct();
    exports.Set("FlatIndex", func);
    return exports;
}

FlatIndexWrap::FlatIndexWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<FlatIndexWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::FlatIndex>>>();
        index_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    const auto dimension = info.Length() > 0 && info[0].IsNumber() ? info[0].As<Napi::Number>().Uint32Value() : vdb::UNIFIED_DIM;
    const auto metric = info.Length() > 1 && info[1].IsNumber() ? static_cast<vdb::DistanceMetric>(info[1].As<Napi::Number>().Uint32Value()) : vdb::DistanceMetric::Cosine;
    index_ = std::make_unique<vdb::FlatIndex>(dimension, metric);
}

Napi::Value FlatIndexWrap::Add(const Napi::CallbackInfo& info) {
    auto result = index_->add(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()), JSToVector(info[1].As<Napi::Array>()));
    UnwrapResult(result, info.Env());
    return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending());
}
Napi::Value FlatIndexWrap::Search(const Napi::CallbackInfo& info) { return QueryResultsToArray(info.Env(), index_->search(JSToVector(info[0].As<Napi::Array>()), info[1].As<Napi::Number>().Uint32Value())); }
Napi::Value FlatIndexWrap::Contains(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), index_->contains(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()))); }
Napi::Value FlatIndexWrap::GetVector(const Napi::CallbackInfo& info) { auto result = index_->get_vector(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())); return result ? VectorToJS(info.Env(), std::vector<float>(result->begin(), result->end())) : info.Env().Null(); }
Napi::Value FlatIndexWrap::Size(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(index_->size())); }
Napi::Value FlatIndexWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), index_->dimension()); }
Napi::Value FlatIndexWrap::Save(const Napi::CallbackInfo& info) { auto result = index_->save(info[0].As<Napi::String>().Utf8Value()); UnwrapResult(result, info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value FlatIndexWrap::Load(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto loaded = vdb::FlatIndex::load(info[0].As<Napi::String>().Utf8Value());
    auto index = UnwrapResult(loaded, env);
    if (env.IsExceptionPending()) return env.Null();
    auto* holder = new std::unique_ptr<vdb::FlatIndex>(std::make_unique<vdb::FlatIndex>(std::move(index)));
    return g_flat_constructor.New({Napi::External<std::unique_ptr<vdb::FlatIndex>>::New(env, holder)});
}

Napi::Object MetadataIndexWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "MetadataIndex", {
        InstanceMethod("add", &MetadataIndexWrap::Add),
        InstanceMethod("remove", &MetadataIndexWrap::Remove),
        InstanceMethod("search", &MetadataIndexWrap::Search),
        InstanceMethod("size", &MetadataIndexWrap::Size),
    });
    g_metadata_constructor = Napi::Persistent(func);
    g_metadata_constructor.SuppressDestruct();
    exports.Set("MetadataIndex", func);
    return exports;
}

MetadataIndexWrap::MetadataIndexWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<MetadataIndexWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::index::MetadataIndex>>>();
        index_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    index_ = std::make_unique<vdb::index::MetadataIndex>();
    (void)index_->create_index("date", false);
    (void)index_->create_index("source_file", false);
    (void)index_->create_index("asset", false);
    (void)index_->create_index("bias", false);
    (void)index_->create_index("gold_price", true);
    (void)index_->create_index("silver_price", true);
    (void)index_->create_index("gsr", true);
    (void)index_->create_index("dxy", true);
    (void)index_->create_index("vix", true);
    (void)index_->create_index("yield_10y", true);
}

Napi::Value MetadataIndexWrap::Add(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto metadata = JSToMetadata(info[0].As<Napi::Object>());
    if (info.Length() > 1 && info[1].IsNumber()) metadata.id = static_cast<vdb::VectorId>(info[1].As<Napi::Number>().Int64Value());
    auto result = index_->insert(metadata.id, metadata);
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value MetadataIndexWrap::Remove(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    auto metadata = JSToMetadata(info[0].As<Napi::Object>());
    if (info.Length() > 1 && info[1].IsNumber()) metadata.id = static_cast<vdb::VectorId>(info[1].As<Napi::Number>().Int64Value());
    auto result = index_->remove(metadata.id, metadata);
    UnwrapResult(result, env);
    return Napi::Boolean::New(env, !env.IsExceptionPending());
}

Napi::Value MetadataIndexWrap::Search(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "Expected filter object").ThrowAsJavaScriptException();
        return env.Null();
    }
    Napi::Object filter = info[0].As<Napi::Object>();
    if (filter.Has("conditions") && filter.Get("conditions").IsArray()) {
        std::vector<vdb::index::FilterCondition> conditions;
        Napi::Array items = filter.Get("conditions").As<Napi::Array>();
        conditions.reserve(items.Length());
        for (uint32_t i = 0; i < items.Length(); ++i) {
            conditions.push_back(ParseFilterCondition(items.Get(i).As<Napi::Object>()));
        }
        const auto mode = filter.Has("mode") ? filter.Get("mode").As<Napi::String>().Utf8Value() : std::string("and");
        return SetToArray(env, mode == "or" ? index_->query_or(conditions) : index_->query_and(conditions));
    }
    return SetToArray(env, index_->query(ParseFilterCondition(filter)));
}

Napi::Value MetadataIndexWrap::Size(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() > 0 && info[0].IsString()) {
        return Napi::Number::New(env, static_cast<double>(index_->size(info[0].As<Napi::String>().Utf8Value())));
    }
    return Napi::Number::New(env, static_cast<double>(index_->total_entries()));
}

} // namespace hektor_native
