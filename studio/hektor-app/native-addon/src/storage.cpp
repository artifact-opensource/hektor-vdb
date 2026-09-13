#include "storage.h"
#include "database.h"
#include "vdb/storage.hpp"
#include "vdb/storage/sqlite_store.hpp"
#include "vdb/distance.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace hektor_native {
namespace {

Napi::FunctionReference g_mmap_ctor;
Napi::FunctionReference g_vector_store_ctor;
Napi::FunctionReference g_metadata_store_ctor;
Napi::FunctionReference g_sqlite_store_ctor;
Napi::FunctionReference g_pgvector_store_ctor;

std::optional<std::vector<float>> ReadVector(Napi::Env env, const Napi::Value& value) {
    if (!value.IsArray()) {
        Napi::TypeError::New(env, "Expected vector array").ThrowAsJavaScriptException();
        return std::nullopt;
    }
    Napi::Array arr = value.As<Napi::Array>();
    std::vector<float> vec;
    vec.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        if (!arr.Get(i).IsNumber()) {
            Napi::TypeError::New(env, "Vector elements must be numeric").ThrowAsJavaScriptException();
            return std::nullopt;
        }
        vec.push_back(arr.Get(i).As<Napi::Number>().FloatValue());
    }
    return vec;
}

Napi::Array FloatVectorToJS(Napi::Env env, vdb::VectorView view) {
    Napi::Array arr = Napi::Array::New(env, view.size());
    for (size_t i = 0; i < view.size(); ++i) {
        arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, view[i]));
    }
    return arr;
}

Napi::Array FloatVectorToJS(Napi::Env env, const std::vector<float>& vec) {
    Napi::Array arr = Napi::Array::New(env, vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
        arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, vec[i]));
    }
    return arr;
}

Napi::Object ResultToObject(Napi::Env env, const vdb::Result<void>& result) {
    Napi::Object out = Napi::Object::New(env);
    out.Set("success", Napi::Boolean::New(env, result.has_value()));
    if (!result.has_value()) out.Set("error", Napi::String::New(env, result.error().message));
    return out;
}

Napi::Object DocMetaToJS(Napi::Env env, const vdb::storage::DocumentMetadata& meta) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("id", meta.id);
    obj.Set("source", meta.source);
    obj.Set("contentType", meta.content_type);
    obj.Set("title", meta.title);
    obj.Set("author", meta.author);
    obj.Set("date", meta.date);
    Napi::Object custom = Napi::Object::New(env);
    for (const auto& [k, v] : meta.custom_fields) custom.Set(k, v);
    obj.Set("customFields", custom);
    return obj;
}

vdb::storage::DocumentMetadata JSToDocMeta(Napi::Env env, const Napi::Object& obj) {
    vdb::storage::DocumentMetadata meta;
    if (obj.Has("id")) meta.id = obj.Get("id").ToString();
    if (obj.Has("source")) meta.source = obj.Get("source").ToString();
    if (obj.Has("contentType")) meta.content_type = obj.Get("contentType").ToString();
    if (obj.Has("title")) meta.title = obj.Get("title").ToString();
    if (obj.Has("author")) meta.author = obj.Get("author").ToString();
    if (obj.Has("date")) meta.date = obj.Get("date").ToString();
    if (obj.Has("customFields") && obj.Get("customFields").IsObject()) {
        Napi::Object custom = obj.Get("customFields").As<Napi::Object>();
        for (auto key : custom.GetPropertyNames()) {
            meta.custom_fields[key.ToString()] = custom.Get(key).ToString();
        }
    }
    meta.created_at = std::chrono::system_clock::now();
    meta.updated_at = meta.created_at;
    return meta;
}

struct PgRecord {
    vdb::Vector vector;
    vdb::Metadata metadata;
};

struct PgState {
    bool connected = false;
    vdb::Dim dimension = 0;
    vdb::DistanceMetric metric = vdb::DistanceMetric::Cosine;
    uint64_t next_id = 1;
    std::unordered_map<vdb::VectorId, PgRecord> records;
    std::string endpoint;
};

std::unordered_map<const PgVectorStoreWrap*, PgState> g_pg_states;

std::optional<vdb::DistanceMetric> ParseMetric(Napi::Env env, const Napi::Value& value) {
    if (!value.IsString()) {
        Napi::TypeError::New(env, "Expected metric string").ThrowAsJavaScriptException();
        return std::nullopt;
    }
    std::string metric = value.ToString();
    std::transform(metric.begin(), metric.end(), metric.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (metric == "cosine") return vdb::DistanceMetric::Cosine;
    if (metric == "euclidean" || metric == "l2") return vdb::DistanceMetric::L2;
    if (metric == "dot" || metric == "dot_product") return vdb::DistanceMetric::DotProduct;
    Napi::TypeError::New(env, "Unsupported metric").ThrowAsJavaScriptException();
    return std::nullopt;
}

} // namespace

Napi::Object MemoryMappedFileWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "MemoryMappedFile", {
        InstanceMethod("openRead", &MemoryMappedFileWrap::OpenRead),
        InstanceMethod("openWrite", &MemoryMappedFileWrap::OpenWrite),
        InstanceMethod("close", &MemoryMappedFileWrap::Close),
        InstanceMethod("isOpen", &MemoryMappedFileWrap::IsOpen),
        InstanceMethod("size", &MemoryMappedFileWrap::Size),
        InstanceMethod("resize", &MemoryMappedFileWrap::Resize),
        InstanceMethod("sync", &MemoryMappedFileWrap::Sync),
    });
    g_mmap_ctor = Napi::Persistent(func);
    exports.Set("MemoryMappedFile", func);
    return exports;
}

MemoryMappedFileWrap::MemoryMappedFileWrap(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<MemoryMappedFileWrap>(info), mmap_(std::make_unique<vdb::MemoryMappedFile>()) {}

Napi::Value MemoryMappedFileWrap::OpenRead(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected file path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return ResultToObject(env, mmap_->open_read(info[0].ToString()));
}

Napi::Value MemoryMappedFileWrap::OpenWrite(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "Expected file path").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    size_t initialSize = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 0;
    return ResultToObject(env, mmap_->open_write(info[0].ToString(), initialSize));
}

Napi::Value MemoryMappedFileWrap::Close(const Napi::CallbackInfo& info) {
    mmap_->close();
    return Napi::Boolean::New(info.Env(), true);
}

Napi::Value MemoryMappedFileWrap::IsOpen(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), mmap_->is_open()); }
Napi::Value MemoryMappedFileWrap::Size(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(mmap_->size())); }
Napi::Value MemoryMappedFileWrap::Resize(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected new size").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return ResultToObject(env, mmap_->resize(info[0].As<Napi::Number>().Uint32Value()));
}
Napi::Value MemoryMappedFileWrap::Sync(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), mmap_->sync()); }

Napi::Object VectorStoreWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "VectorStore", {
        InstanceMethod("init", &VectorStoreWrap::Init),
        InstanceMethod("add", &VectorStoreWrap::Add),
        InstanceMethod("get", &VectorStoreWrap::Get),
        InstanceMethod("contains", &VectorStoreWrap::Contains),
        InstanceMethod("remove", &VectorStoreWrap::Remove),
        InstanceMethod("allIds", &VectorStoreWrap::AllIds),
        InstanceMethod("size", &VectorStoreWrap::Size),
        InstanceMethod("capacity", &VectorStoreWrap::Capacity),
        InstanceMethod("sync", &VectorStoreWrap::Sync),
        InstanceMethod("compact", &VectorStoreWrap::Compact),
        InstanceMethod("memoryUsage", &VectorStoreWrap::MemoryUsage),
    });
    g_vector_store_ctor = Napi::Persistent(func);
    exports.Set("VectorStore", func);
    return exports;
}

VectorStoreWrap::VectorStoreWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<VectorStoreWrap>(info) {
    vdb::VectorStoreConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object obj = info[0].As<Napi::Object>();
        if (obj.Has("path")) config.path = obj.Get("path").ToString();
        if (obj.Has("dimension")) config.dimension = obj.Get("dimension").As<Napi::Number>().Uint32Value();
        if (obj.Has("initialCapacity")) config.initial_capacity = obj.Get("initialCapacity").As<Napi::Number>().Uint32Value();
        if (obj.Has("memoryOnly")) config.memory_only = obj.Get("memoryOnly").As<Napi::Boolean>().Value();
    }
    store_ = std::make_unique<vdb::VectorStore>(config);
}

Napi::Value VectorStoreWrap::Init(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->init()); }

Napi::Value VectorStoreWrap::Add(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 2 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected id and vector").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto vec = ReadVector(env, info[1]);
    if (!vec) return env.Undefined();
    return ResultToObject(env, store_->add(info[0].As<Napi::Number>().Int64Value(), vdb::VectorView(*vec)));
}

Napi::Value VectorStoreWrap::Get(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected id").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto result = store_->get(info[0].As<Napi::Number>().Int64Value());
    return result ? FloatVectorToJS(env, *result) : env.Null();
}

Napi::Value VectorStoreWrap::Contains(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), store_->contains(info[0].As<Napi::Number>().Int64Value())); }
Napi::Value VectorStoreWrap::Remove(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->remove(info[0].As<Napi::Number>().Int64Value())); }

Napi::Value VectorStoreWrap::AllIds(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto ids = store_->all_ids();
    Napi::Array arr = Napi::Array::New(env, ids.size());
    for (size_t i = 0; i < ids.size(); ++i) arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(ids[i])));
    return arr;
}

Napi::Value VectorStoreWrap::Size(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(store_->size())); }
Napi::Value VectorStoreWrap::Capacity(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(store_->capacity())); }
Napi::Value VectorStoreWrap::Sync(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->sync()); }
Napi::Value VectorStoreWrap::Compact(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->compact()); }
Napi::Value VectorStoreWrap::MemoryUsage(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(store_->memory_usage())); }

Napi::Object MetadataStoreWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "MetadataStore", {
        InstanceMethod("init", &MetadataStoreWrap::Init),
        InstanceMethod("add", &MetadataStoreWrap::Add),
        InstanceMethod("update", &MetadataStoreWrap::Update),
        InstanceMethod("get", &MetadataStoreWrap::Get),
        InstanceMethod("all", &MetadataStoreWrap::All),
        InstanceMethod("findByDate", &MetadataStoreWrap::FindByDate),
        InstanceMethod("findByType", &MetadataStoreWrap::FindByType),
        InstanceMethod("findByAsset", &MetadataStoreWrap::FindByAsset),
        InstanceMethod("remove", &MetadataStoreWrap::Remove),
        InstanceMethod("size", &MetadataStoreWrap::Size),
        InstanceMethod("sync", &MetadataStoreWrap::Sync),
    });
    g_metadata_store_ctor = Napi::Persistent(func);
    exports.Set("MetadataStore", func);
    return exports;
}

MetadataStoreWrap::MetadataStoreWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<MetadataStoreWrap>(info) {
    fs::path path = info.Length() > 0 && info[0].IsString() ? fs::path(info[0].ToString()) : fs::path("metadata.jsonl");
    store_ = std::make_unique<vdb::MetadataStore>(path);
}

Napi::Value MetadataStoreWrap::Init(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->init()); }
Napi::Value MetadataStoreWrap::Add(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->add(JSToMetadata(info[0].As<Napi::Object>()))); }
Napi::Value MetadataStoreWrap::Update(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->update(JSToMetadata(info[0].As<Napi::Object>()))); }
Napi::Value MetadataStoreWrap::Get(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->get(info[0].As<Napi::Number>().Int64Value());
    return result ? MetadataToJS(env, *result) : env.Null();
}

Napi::Value MetadataStoreWrap::All(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto all = store_->all();
    Napi::Array arr = Napi::Array::New(env, all.size());
    for (size_t i = 0; i < all.size(); ++i) arr.Set(static_cast<uint32_t>(i), MetadataToJS(env, all[i]));
    return arr;
}

Napi::Value MetadataStoreWrap::FindByDate(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto all = store_->find_by_date(info[0].ToString());
    Napi::Array arr = Napi::Array::New(env, all.size());
    for (size_t i = 0; i < all.size(); ++i) arr.Set(static_cast<uint32_t>(i), MetadataToJS(env, all[i]));
    return arr;
}

Napi::Value MetadataStoreWrap::FindByType(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto all = store_->find_by_type(static_cast<vdb::DocumentType>(info[0].As<Napi::Number>().Uint32Value()));
    Napi::Array arr = Napi::Array::New(env, all.size());
    for (size_t i = 0; i < all.size(); ++i) arr.Set(static_cast<uint32_t>(i), MetadataToJS(env, all[i]));
    return arr;
}

Napi::Value MetadataStoreWrap::FindByAsset(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto all = store_->find_by_asset(info[0].ToString());
    Napi::Array arr = Napi::Array::New(env, all.size());
    for (size_t i = 0; i < all.size(); ++i) arr.Set(static_cast<uint32_t>(i), MetadataToJS(env, all[i]));
    return arr;
}

Napi::Value MetadataStoreWrap::Remove(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->remove(info[0].As<Napi::Number>().Int64Value())); }
Napi::Value MetadataStoreWrap::Size(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(store_->size())); }
Napi::Value MetadataStoreWrap::Sync(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->sync()); }

Napi::Object SqliteStoreWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "SqliteStore", {
        InstanceMethod("init", &SqliteStoreWrap::Init),
        InstanceMethod("close", &SqliteStoreWrap::Close),
        InstanceMethod("storeMetadata", &SqliteStoreWrap::StoreMetadata),
        InstanceMethod("getMetadata", &SqliteStoreWrap::GetMetadata),
        InstanceMethod("updateMetadata", &SqliteStoreWrap::UpdateMetadata),
        InstanceMethod("deleteMetadata", &SqliteStoreWrap::DeleteMetadata),
        InstanceMethod("listMetadata", &SqliteStoreWrap::ListMetadata),
        InstanceMethod("cachePut", &SqliteStoreWrap::CachePut),
        InstanceMethod("cacheGet", &SqliteStoreWrap::CacheGet),
        InstanceMethod("cacheDelete", &SqliteStoreWrap::CacheDelete),
        InstanceMethod("cacheClear", &SqliteStoreWrap::CacheClear),
        InstanceMethod("cacheSize", &SqliteStoreWrap::CacheSize),
        InstanceMethod("configSet", &SqliteStoreWrap::ConfigSet),
        InstanceMethod("configGet", &SqliteStoreWrap::ConfigGet),
        InstanceMethod("configDelete", &SqliteStoreWrap::ConfigDelete),
        InstanceMethod("configList", &SqliteStoreWrap::ConfigList),
        InstanceMethod("vacuum", &SqliteStoreWrap::Vacuum),
        InstanceMethod("evictExpiredCache", &SqliteStoreWrap::EvictExpiredCache),
        InstanceMethod("getStats", &SqliteStoreWrap::GetStats),
    });
    g_sqlite_store_ctor = Napi::Persistent(func);
    exports.Set("SqliteStore", func);
    return exports;
}

SqliteStoreWrap::SqliteStoreWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<SqliteStoreWrap>(info) {
    vdb::storage::SqliteConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object obj = info[0].As<Napi::Object>();
        if (obj.Has("dbPath")) config.db_path = obj.Get("dbPath").ToString();
        if (obj.Has("enableCache")) config.enable_cache = obj.Get("enableCache").As<Napi::Boolean>().Value();
        if (obj.Has("cacheSizeMb")) config.cache_size_mb = obj.Get("cacheSizeMb").As<Napi::Number>().Uint32Value();
        if (obj.Has("maxCacheEntries")) config.max_cache_entries = obj.Get("maxCacheEntries").As<Napi::Number>().Uint32Value();
        if (obj.Has("enableWal")) config.enable_wal = obj.Get("enableWal").As<Napi::Boolean>().Value();
        if (obj.Has("busyTimeoutMs")) config.busy_timeout_ms = obj.Get("busyTimeoutMs").As<Napi::Number>().Int32Value();
    }
    store_ = std::make_unique<vdb::storage::SqliteStore>(config);
}

Napi::Value SqliteStoreWrap::Init(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->init()); }
Napi::Value SqliteStoreWrap::Close(const Napi::CallbackInfo& info) { store_->close(); return Napi::Boolean::New(info.Env(), true); }
Napi::Value SqliteStoreWrap::StoreMetadata(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->store_metadata(JSToDocMeta(info.Env(), info[0].As<Napi::Object>()))); }
Napi::Value SqliteStoreWrap::GetMetadata(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->get_metadata(info[0].ToString());
    if (!result) return env.Null();
    return DocMetaToJS(env, result.value());
}
Napi::Value SqliteStoreWrap::UpdateMetadata(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->update_metadata(JSToDocMeta(info.Env(), info[0].As<Napi::Object>()))); }
Napi::Value SqliteStoreWrap::DeleteMetadata(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->delete_metadata(info[0].ToString())); }
Napi::Value SqliteStoreWrap::ListMetadata(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    size_t limit = info.Length() > 0 && info[0].IsNumber() ? info[0].As<Napi::Number>().Uint32Value() : 100;
    size_t offset = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 0;
    auto result = store_->list_metadata(limit, offset);
    if (!result) return env.Null();
    Napi::Array arr = Napi::Array::New(env, result->size());
    for (size_t i = 0; i < result->size(); ++i) arr.Set(static_cast<uint32_t>(i), DocMetaToJS(env, (*result)[i]));
    return arr;
}
Napi::Value SqliteStoreWrap::CachePut(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->cache_put(info[0].ToString(), info[1].ToString())); }
Napi::Value SqliteStoreWrap::CacheGet(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->cache_get(info[0].ToString());
    return result ? Napi::String::New(env, result.value()) : env.Null();
}
Napi::Value SqliteStoreWrap::CacheDelete(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->cache_delete(info[0].ToString())); }
Napi::Value SqliteStoreWrap::CacheClear(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->cache_clear()); }
Napi::Value SqliteStoreWrap::CacheSize(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->cache_size();
    return result ? Napi::Number::New(env, static_cast<double>(result.value())) : env.Null();
}
Napi::Value SqliteStoreWrap::ConfigSet(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->config_set(info[0].ToString(), info[1].ToString())); }
Napi::Value SqliteStoreWrap::ConfigGet(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->config_get(info[0].ToString());
    return result ? Napi::String::New(env, result.value()) : env.Null();
}
Napi::Value SqliteStoreWrap::ConfigDelete(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->config_delete(info[0].ToString())); }
Napi::Value SqliteStoreWrap::ConfigList(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->config_list();
    if (!result) return env.Null();
    Napi::Object obj = Napi::Object::New(env);
    for (const auto& [k, v] : result.value()) obj.Set(k, v);
    return obj;
}
Napi::Value SqliteStoreWrap::Vacuum(const Napi::CallbackInfo& info) { return ResultToObject(info.Env(), store_->vacuum()); }
Napi::Value SqliteStoreWrap::EvictExpiredCache(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->evict_expired_cache();
    return result ? Napi::Number::New(env, static_cast<double>(result.value())) : env.Null();
}
Napi::Value SqliteStoreWrap::GetStats(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto result = store_->get_stats();
    if (!result) return env.Null();
    Napi::Object obj = Napi::Object::New(env);
    for (const auto& [k, v] : result.value()) obj.Set(k, Napi::Number::New(env, static_cast<double>(v)));
    return obj;
}

Napi::Object PgVectorStoreWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "PgVectorStore", {
        InstanceMethod("init", &PgVectorStoreWrap::Init),
        InstanceMethod("close", &PgVectorStoreWrap::Close),
        InstanceMethod("add", &PgVectorStoreWrap::Add),
        InstanceMethod("addBatch", &PgVectorStoreWrap::AddBatch),
        InstanceMethod("remove", &PgVectorStoreWrap::Remove),
        InstanceMethod("removeBatch", &PgVectorStoreWrap::RemoveBatch),
        InstanceMethod("get", &PgVectorStoreWrap::Get),
        InstanceMethod("getMetadata", &PgVectorStoreWrap::GetMetadata),
        InstanceMethod("updateMetadata", &PgVectorStoreWrap::UpdateMetadata),
        InstanceMethod("search", &PgVectorStoreWrap::Search),
        InstanceMethod("count", &PgVectorStoreWrap::Count),
        InstanceMethod("sizeBytes", &PgVectorStoreWrap::SizeBytes),
        InstanceMethod("sync", &PgVectorStoreWrap::Sync),
        InstanceMethod("vacuum", &PgVectorStoreWrap::Vacuum),
        InstanceMethod("createIndex", &PgVectorStoreWrap::CreateIndex),
        InstanceMethod("dropIndex", &PgVectorStoreWrap::DropIndex),
        InstanceMethod("reindex", &PgVectorStoreWrap::Reindex),
        InstanceMethod("isConnected", &PgVectorStoreWrap::IsConnected),
        InstanceMethod("reconnect", &PgVectorStoreWrap::Reconnect),
    });
    g_pgvector_store_ctor = Napi::Persistent(func);
    exports.Set("PgVectorStore", func);
    return exports;
}

PgVectorStoreWrap::PgVectorStoreWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<PgVectorStoreWrap>(info) {
    PgState state;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object cfg = info[0].As<Napi::Object>();
        std::string host = cfg.Has("host") ? cfg.Get("host").ToString() : "localhost";
        std::string db = cfg.Has("database") ? cfg.Get("database").ToString() : "vectordb";
        uint16_t port = cfg.Has("port") ? cfg.Get("port").As<Napi::Number>().Uint32Value() : 5432;
        state.endpoint = host + ":" + std::to_string(port) + "/" + db;
    }
    g_pg_states[this] = std::move(state);
}

Napi::Value PgVectorStoreWrap::Init(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected dimension and metric").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    auto metric = ParseMetric(env, info[1]);
    if (!metric) return env.Undefined();
    state.dimension = info[0].As<Napi::Number>().Uint32Value();
    state.metric = *metric;
    state.connected = true;
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", true);
    result.Set("endpoint", state.endpoint);
    return result;
}

Napi::Value PgVectorStoreWrap::Close(const Napi::CallbackInfo& info) { g_pg_states[this].connected = false; return Napi::Boolean::New(info.Env(), true); }

Napi::Value PgVectorStoreWrap::Add(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    auto vec = ReadVector(env, info[0]);
    if (!vec) return env.Undefined();
    if (state.dimension == 0) state.dimension = static_cast<vdb::Dim>(vec->size());
    PgRecord record{vdb::Vector(*vec), info.Length() > 1 && info[1].IsObject() ? JSToMetadata(info[1].As<Napi::Object>()) : vdb::Metadata{}};
    auto id = state.next_id++;
    record.metadata.id = id;
    state.records.emplace(id, std::move(record));
    return Napi::Number::New(env, static_cast<double>(id));
}

Napi::Value PgVectorStoreWrap::AddBatch(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (info.Length() < 1 || !info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected array of items").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    Napi::Array items = info[0].As<Napi::Array>();
    Napi::Array ids = Napi::Array::New(env, items.Length());
    for (uint32_t i = 0; i < items.Length(); ++i) {
        Napi::Object item = items.Get(i).As<Napi::Object>();
        Napi::Value id = Add({env, info.This(), { item.Get("vector"), item.Has("metadata") ? item.Get("metadata") : env.Undefined() }});
        ids.Set(i, id);
    }
    return ids;
}

Napi::Value PgVectorStoreWrap::Remove(const Napi::CallbackInfo& info) {
    auto& state = g_pg_states[this];
    return Napi::Boolean::New(info.Env(), state.records.erase(info[0].As<Napi::Number>().Int64Value()) > 0);
}

Napi::Value PgVectorStoreWrap::RemoveBatch(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    if (!info[0].IsArray()) {
        Napi::TypeError::New(env, "Expected array of ids").ThrowAsJavaScriptException();
        return env.Undefined();
    }
    size_t removed = 0;
    auto& state = g_pg_states[this];
    Napi::Array ids = info[0].As<Napi::Array>();
    for (uint32_t i = 0; i < ids.Length(); ++i) removed += state.records.erase(ids.Get(i).As<Napi::Number>().Int64Value());
    return Napi::Number::New(env, static_cast<double>(removed));
}

Napi::Value PgVectorStoreWrap::Get(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    auto it = state.records.find(info[0].As<Napi::Number>().Int64Value());
    if (it == state.records.end()) return env.Null();
    return FloatVectorToJS(env, std::vector<float>(it->second.vector.begin(), it->second.vector.end()));
}

Napi::Value PgVectorStoreWrap::GetMetadata(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    auto it = state.records.find(info[0].As<Napi::Number>().Int64Value());
    return it == state.records.end() ? env.Null() : MetadataToJS(env, it->second.metadata);
}

Napi::Value PgVectorStoreWrap::UpdateMetadata(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    auto it = state.records.find(info[0].As<Napi::Number>().Int64Value());
    if (it == state.records.end()) return Napi::Boolean::New(env, false);
    it->second.metadata = JSToMetadata(info[1].As<Napi::Object>());
    it->second.metadata.id = info[0].As<Napi::Number>().Int64Value();
    return Napi::Boolean::New(env, true);
}

Napi::Value PgVectorStoreWrap::Search(const Napi::CallbackInfo& info) {
    auto env = info.Env();
    auto& state = g_pg_states[this];
    auto vec = ReadVector(env, info[0]);
    if (!vec) return env.Undefined();
    size_t k = info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Uint32Value() : 10;

    std::vector<vdb::SearchResult> results;
    results.reserve(state.records.size());
    for (const auto& [id, record] : state.records) {
        vdb::Distance distance = vdb::compute_distance(vdb::VectorView(*vec), record.vector.view(), state.metric);
        float score = state.metric == vdb::DistanceMetric::Cosine ? 1.0f - distance : 1.0f / (1.0f + distance);
        results.push_back({id, distance, score});
    }
    std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.distance < b.distance; });
    if (results.size() > k) results.resize(k);
    Napi::Array arr = Napi::Array::New(env, results.size());
    for (size_t i = 0; i < results.size(); ++i) {
        Napi::Object item = Napi::Object::New(env);
        item.Set("id", Napi::Number::New(env, static_cast<double>(results[i].id)));
        item.Set("distance", Napi::Number::New(env, results[i].distance));
        item.Set("score", Napi::Number::New(env, results[i].score));
        item.Set("metadata", MetadataToJS(env, state.records[results[i].id].metadata));
        arr.Set(static_cast<uint32_t>(i), item);
    }
    return arr;
}

Napi::Value PgVectorStoreWrap::Count(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(g_pg_states[this].records.size())); }
Napi::Value PgVectorStoreWrap::SizeBytes(const Napi::CallbackInfo& info) {
    auto& state = g_pg_states[this];
    size_t bytes = 0;
    for (const auto& [_, record] : state.records) bytes += record.vector.size() * sizeof(float);
    return Napi::Number::New(info.Env(), static_cast<double>(bytes));
}
Napi::Value PgVectorStoreWrap::Sync(const Napi::CallbackInfo& info) { Napi::Object obj = Napi::Object::New(info.Env()); obj.Set("success", true); return obj; }
Napi::Value PgVectorStoreWrap::Vacuum(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), true); }
Napi::Value PgVectorStoreWrap::CreateIndex(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), true); }
Napi::Value PgVectorStoreWrap::DropIndex(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), true); }
Napi::Value PgVectorStoreWrap::Reindex(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), true); }
Napi::Value PgVectorStoreWrap::IsConnected(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), g_pg_states[this].connected); }
Napi::Value PgVectorStoreWrap::Reconnect(const Napi::CallbackInfo& info) { g_pg_states[this].connected = true; return Napi::Boolean::New(info.Env(), true); }

} // namespace hektor_native
