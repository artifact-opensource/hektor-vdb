#include "rag.h"

#include "database.h"
#include "vdb/framework_integration.hpp"
#include "vdb/llm/llm_engine.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace hektor_native {
namespace {

Napi::FunctionReference g_rag_constructor;
Napi::FunctionReference g_llm_constructor;
Napi::FunctionReference g_langchain_constructor;
Napi::FunctionReference g_llamaindex_constructor;
Napi::FunctionReference g_chunker_constructor;

std::vector<std::string> StringVector(const Napi::Array& arr) {
    std::vector<std::string> out;
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) out.push_back(arr.Get(i).ToString().Utf8Value());
    return out;
}

std::vector<vdb::Metadata> MetadataVector(Napi::Env env, const Napi::Value& value) {
    std::vector<vdb::Metadata> out;
    if (!value.IsArray()) return out;
    Napi::Array arr = value.As<Napi::Array>();
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) out.push_back(JSToMetadata(arr.Get(i).As<Napi::Object>()));
    return out;
}

Napi::Object RAGContextToJs(Napi::Env env, const vdb::framework::RAGContext& context) {
    Napi::Object obj = Napi::Object::New(env);
    Napi::Array chunks = Napi::Array::New(env, context.retrieved_chunks.size());
    Napi::Array scores = Napi::Array::New(env, context.relevance_scores.size());
    Napi::Array ids = Napi::Array::New(env, context.document_ids.size());
    for (size_t i = 0; i < context.retrieved_chunks.size(); ++i) chunks.Set(static_cast<uint32_t>(i), Napi::String::New(env, context.retrieved_chunks[i]));
    for (size_t i = 0; i < context.relevance_scores.size(); ++i) scores.Set(static_cast<uint32_t>(i), Napi::Number::New(env, context.relevance_scores[i]));
    for (size_t i = 0; i < context.document_ids.size(); ++i) ids.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(context.document_ids[i])));
    obj.Set("retrievedChunks", chunks);
    obj.Set("relevanceScores", scores);
    obj.Set("documentIds", ids);
    obj.Set("formattedContext", Napi::String::New(env, context.formatted_context));
    obj.Set("totalTokens", Napi::Number::New(env, static_cast<double>(context.total_tokens)));
    return obj;
}

std::vector<vdb::QueryResult> QueryResults(const Napi::Array& arr) {
    std::vector<vdb::QueryResult> out;
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        Napi::Object obj = arr.Get(i).As<Napi::Object>();
        vdb::QueryResult result;
        result.id = static_cast<vdb::VectorId>(obj.Get("id").As<Napi::Number>().Int64Value());
        result.distance = obj.Has("distance") ? obj.Get("distance").As<Napi::Number>().FloatValue() : 0.0f;
        result.score = obj.Has("score") ? obj.Get("score").As<Napi::Number>().FloatValue() : (1.0f / (1.0f + result.distance));
        if (obj.Has("metadata") && obj.Get("metadata").IsObject()) result.metadata = JSToMetadata(obj.Get("metadata").As<Napi::Object>());
        out.push_back(std::move(result));
    }
    return out;
}

std::vector<vdb::llm::Message> Messages(const Napi::Array& arr) {
    std::vector<vdb::llm::Message> out;
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) {
        Napi::Object msg = arr.Get(i).As<Napi::Object>();
        std::string role = msg.Get("role").ToString().Utf8Value();
        vdb::llm::Role parsed = vdb::llm::Role::User;
        if (role == "system") parsed = vdb::llm::Role::System;
        else if (role == "assistant") parsed = vdb::llm::Role::Assistant;
        out.push_back({parsed, msg.Get("content").ToString().Utf8Value()});
    }
    return out;
}

vdb::llm::GenerationParams Generation(const Napi::Value& value) {
    vdb::llm::GenerationParams params;
    if (!value.IsObject()) return params;
    Napi::Object object = value.As<Napi::Object>();
    if (object.Has("maxTokens")) params.max_tokens = object.Get("maxTokens").As<Napi::Number>().Int32Value();
    if (object.Has("temperature")) params.temperature = object.Get("temperature").As<Napi::Number>().FloatValue();
    if (object.Has("topP")) params.top_p = object.Get("topP").As<Napi::Number>().FloatValue();
    if (object.Has("topK")) params.top_k = object.Get("topK").As<Napi::Number>().Int32Value();
    if (object.Has("repeatPenalty")) params.repeat_penalty = object.Get("repeatPenalty").As<Napi::Number>().FloatValue();
    if (object.Has("repeatLastN")) params.repeat_last_n = object.Get("repeatLastN").As<Napi::Number>().Int32Value();
    if (object.Has("stopSequences") && object.Get("stopSequences").IsArray()) params.stop_sequences = StringVector(object.Get("stopSequences").As<Napi::Array>());
    return params;
}

} // namespace

Napi::Object RAGEngineWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "RAGEngine", {
        InstanceMethod("chunkDocument", &RAGEngineWrap::ChunkDocument),
        InstanceMethod("buildContext", &RAGEngineWrap::BuildContext),
        InstanceMethod("formatPrompt", &RAGEngineWrap::FormatPrompt),
        InstanceMethod("rerank", &RAGEngineWrap::Rerank),
    });
    g_rag_constructor = Napi::Persistent(func);
    g_rag_constructor.SuppressDestruct();
    exports.Set("RAGEngine", func);
    return exports;
}

RAGEngineWrap::RAGEngineWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<RAGEngineWrap>(info) {
    vdb::framework::RAGConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto options = info[0].As<Napi::Object>();
        if (options.Has("topK")) config.top_k = options.Get("topK").As<Napi::Number>().Int64Value();
        if (options.Has("maxContextLength")) config.max_context_length = options.Get("maxContextLength").As<Napi::Number>().Int64Value();
        if (options.Has("relevanceThreshold")) config.relevance_threshold = options.Get("relevanceThreshold").As<Napi::Number>().FloatValue();
        if (options.Has("rerank")) config.rerank = options.Get("rerank").As<Napi::Boolean>().Value();
        if (options.Has("chunkingStrategy")) config.chunking_strategy = options.Get("chunkingStrategy").ToString().Utf8Value();
        if (options.Has("chunkSize")) config.chunk_size = options.Get("chunkSize").As<Napi::Number>().Int64Value();
        if (options.Has("chunkOverlap")) config.chunk_overlap = options.Get("chunkOverlap").As<Napi::Number>().Int64Value();
    }
    engine_ = std::make_unique<vdb::framework::RAGEngine>(config);
}

Napi::Value RAGEngineWrap::ChunkDocument(const Napi::CallbackInfo& info) {
    auto result = engine_->chunk_document(info[0].ToString().Utf8Value());
    auto chunks = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    Napi::Array out = Napi::Array::New(info.Env(), chunks.size());
    for (size_t i = 0; i < chunks.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), chunks[i]));
    return out;
}

Napi::Value RAGEngineWrap::BuildContext(const Napi::CallbackInfo& info) {
    auto result = engine_->build_context(info[0].ToString().Utf8Value(), QueryResults(info[1].As<Napi::Array>()));
    auto context = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    return RAGContextToJs(info.Env(), context);
}

Napi::Value RAGEngineWrap::FormatPrompt(const Napi::CallbackInfo& info) {
    vdb::framework::RAGContext context;
    Napi::Object input = info[1].As<Napi::Object>();
    if (input.Has("retrievedChunks")) context.retrieved_chunks = StringVector(input.Get("retrievedChunks").As<Napi::Array>());
    if (input.Has("relevanceScores") && input.Get("relevanceScores").IsArray()) {
        Napi::Array scores = input.Get("relevanceScores").As<Napi::Array>();
        for (uint32_t i = 0; i < scores.Length(); ++i) context.relevance_scores.push_back(scores.Get(i).As<Napi::Number>().FloatValue());
    }
    if (input.Has("documentIds") && input.Get("documentIds").IsArray()) {
        Napi::Array ids = input.Get("documentIds").As<Napi::Array>();
        for (uint32_t i = 0; i < ids.Length(); ++i) context.document_ids.push_back(static_cast<vdb::VectorId>(ids.Get(i).As<Napi::Number>().Int64Value()));
    }
    if (input.Has("formattedContext")) context.formatted_context = input.Get("formattedContext").ToString().Utf8Value();
    if (input.Has("totalTokens")) context.total_tokens = input.Get("totalTokens").As<Napi::Number>().Int64Value();
    return Napi::String::New(info.Env(), engine_->format_prompt(info[0].ToString().Utf8Value(), context, info.Length() > 2 ? info[2].ToString().Utf8Value() : std::string()));
}

Napi::Value RAGEngineWrap::Rerank(const Napi::CallbackInfo& info) {
    auto result = engine_->rerank(info[0].ToString().Utf8Value(), QueryResults(info[1].As<Napi::Array>()));
    auto reranked = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    Napi::Array out = Napi::Array::New(info.Env(), reranked.size());
    for (size_t i = 0; i < reranked.size(); ++i) {
        Napi::Object obj = Napi::Object::New(info.Env());
        obj.Set("id", Napi::Number::New(info.Env(), static_cast<double>(reranked[i].id)));
        obj.Set("distance", Napi::Number::New(info.Env(), reranked[i].distance));
        obj.Set("score", Napi::Number::New(info.Env(), reranked[i].score));
        if (reranked[i].metadata) obj.Set("metadata", MetadataToJS(info.Env(), *reranked[i].metadata));
        out.Set(static_cast<uint32_t>(i), obj);
    }
    return out;
}

Napi::Object LLMEngineWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "LLMEngine", {
        InstanceMethod("load", &LLMEngineWrap::Load),
        InstanceMethod("isLoaded", &LLMEngineWrap::IsLoaded),
        InstanceMethod("unload", &LLMEngineWrap::Unload),
        InstanceMethod("modelName", &LLMEngineWrap::ModelName),
        InstanceMethod("contextSize", &LLMEngineWrap::ContextSize),
        InstanceMethod("vocabSize", &LLMEngineWrap::VocabSize),
        InstanceMethod("generate", &LLMEngineWrap::Generate),
        InstanceMethod("chat", &LLMEngineWrap::Chat),
        InstanceMethod("countTokens", &LLMEngineWrap::CountTokens),
        InstanceMethod("embed", &LLMEngineWrap::Embed),
    });
    g_llm_constructor = Napi::Persistent(func);
    g_llm_constructor.SuppressDestruct();
    exports.Set("LLMEngine", func);
    return exports;
}

LLMEngineWrap::LLMEngineWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<LLMEngineWrap>(info) {
    engine_ = vdb::llm::create_llm_engine();
}

Napi::Value LLMEngineWrap::Load(const Napi::CallbackInfo& info) {
    vdb::llm::LLMConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto options = info[0].As<Napi::Object>();
        if (options.Has("modelPath")) config.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
        if (options.Has("contextSize")) config.n_ctx = options.Get("contextSize").As<Napi::Number>().Int32Value();
        if (options.Has("batchSize")) config.n_batch = options.Get("batchSize").As<Napi::Number>().Int32Value();
        if (options.Has("threads")) config.n_threads = options.Get("threads").As<Napi::Number>().Int32Value();
        if (options.Has("gpuLayers")) config.n_gpu_layers = options.Get("gpuLayers").As<Napi::Number>().Int32Value();
        if (options.Has("useMmap")) config.use_mmap = options.Get("useMmap").As<Napi::Boolean>().Value();
        if (options.Has("useMlock")) config.use_mlock = options.Get("useMlock").As<Napi::Boolean>().Value();
    }
    return Napi::Boolean::New(info.Env(), engine_ && engine_->load(config));
}
Napi::Value LLMEngineWrap::IsLoaded(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), engine_ && engine_->is_loaded()); }
Napi::Value LLMEngineWrap::Unload(const Napi::CallbackInfo& info) { if (engine_) engine_->unload(); return info.Env().Undefined(); }
Napi::Value LLMEngineWrap::ModelName(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), engine_ ? engine_->model_name() : std::string()); }
Napi::Value LLMEngineWrap::ContextSize(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), engine_ ? engine_->context_size() : 0); }
Napi::Value LLMEngineWrap::VocabSize(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), engine_ ? engine_->vocab_size() : 0); }
Napi::Value LLMEngineWrap::Generate(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), engine_ ? engine_->generate(info[0].ToString().Utf8Value(), Generation(info.Length() > 1 ? info[1] : info.Env().Undefined())) : std::string()); }
Napi::Value LLMEngineWrap::Chat(const Napi::CallbackInfo& info) {
    auto result = engine_->chat(Messages(info[0].As<Napi::Array>()), Generation(info.Length() > 1 ? info[1] : info.Env().Undefined()));
    Napi::Object obj = Napi::Object::New(info.Env());
    obj.Set("content", Napi::String::New(info.Env(), result.content));
    obj.Set("tokensGenerated", Napi::Number::New(info.Env(), result.tokens_generated));
    obj.Set("tokensPrompt", Napi::Number::New(info.Env(), result.tokens_prompt));
    obj.Set("generationTimeMs", Napi::Number::New(info.Env(), result.generation_time_ms));
    obj.Set("stoppedByEos", Napi::Boolean::New(info.Env(), result.stopped_by_eos));
    obj.Set("stopReason", Napi::String::New(info.Env(), result.stop_reason));
    return obj;
}
Napi::Value LLMEngineWrap::CountTokens(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), engine_ ? engine_->count_tokens(info[0].ToString().Utf8Value()) : 0); }
Napi::Value LLMEngineWrap::Embed(const Napi::CallbackInfo& info) {
    auto embedded = engine_ ? engine_->embed(info[0].ToString().Utf8Value()) : std::optional<std::vector<float>>{};
    return embedded ? VectorToJS(info.Env(), *embedded) : info.Env().Null();
}

Napi::Object LangChainAdapterWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "LangChainAdapter", {
        InstanceMethod("similaritySearch", &LangChainAdapterWrap::SimilaritySearch),
        InstanceMethod("similaritySearchWithScore", &LangChainAdapterWrap::SimilaritySearchWithScore),
        InstanceMethod("maxMarginalRelevanceSearch", &LangChainAdapterWrap::MaxMarginalRelevanceSearch),
        InstanceMethod("asRetriever", &LangChainAdapterWrap::AsRetriever),
    });
    g_langchain_constructor = Napi::Persistent(func);
    g_langchain_constructor.SuppressDestruct();
    exports.Set("LangChainAdapter", func);
    return exports;
}

LangChainAdapterWrap::LangChainAdapterWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<LangChainAdapterWrap>(info) {
    vdb::framework::LangChainConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto options = info[0].As<Napi::Object>();
        if (options.Has("vectorstoreType")) config.vectorstore_type = options.Get("vectorstoreType").ToString().Utf8Value();
        if (options.Has("useMmr")) config.use_mmr = options.Get("useMmr").As<Napi::Boolean>().Value();
        if (options.Has("mmrLambda")) config.mmr_lambda = options.Get("mmrLambda").As<Napi::Number>().FloatValue();
        if (options.Has("fetchK")) config.fetch_k = options.Get("fetchK").As<Napi::Number>().Int64Value();
    }
    adapter_ = std::make_unique<vdb::framework::LangChainAdapter>(config);
}

Napi::Value LangChainAdapterWrap::SimilaritySearch(const Napi::CallbackInfo& info) { auto result = adapter_->similarity_search(info[0].ToString().Utf8Value(), info.Length() > 1 ? info[1].As<Napi::Number>().Uint32Value() : 4); auto items = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), items.size()); for (size_t i = 0; i < items.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), items[i].metadata ? items[i].metadata->source_file : std::to_string(items[i].id))); return out; }
Napi::Value LangChainAdapterWrap::SimilaritySearchWithScore(const Napi::CallbackInfo& info) { auto result = adapter_->similarity_search_with_score(info[0].ToString().Utf8Value(), info.Length() > 1 ? info[1].As<Napi::Number>().Uint32Value() : 4); auto items = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), items.size()); for (size_t i = 0; i < items.size(); ++i) { Napi::Object item = Napi::Object::New(info.Env()); item.Set("id", Napi::Number::New(info.Env(), static_cast<double>(items[i].id))); item.Set("score", Napi::Number::New(info.Env(), items[i].score)); out.Set(static_cast<uint32_t>(i), item);} return out; }
Napi::Value LangChainAdapterWrap::MaxMarginalRelevanceSearch(const Napi::CallbackInfo& info) { auto result = adapter_->max_marginal_relevance_search(info[0].ToString().Utf8Value(), info.Length() > 1 ? info[1].As<Napi::Number>().Uint32Value() : 4, info.Length() > 2 ? info[2].As<Napi::Number>().Uint32Value() : 20, info.Length() > 3 ? info[3].As<Napi::Number>().FloatValue() : 0.5f); auto items = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), items.size()); for (size_t i = 0; i < items.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::Number::New(info.Env(), static_cast<double>(items[i].id))); return out; }
Napi::Value LangChainAdapterWrap::AsRetriever(const Napi::CallbackInfo& info) { auto result = adapter_->as_retriever(info[0].ToString().Utf8Value(), info.Length() > 1 ? info[1].As<Napi::Number>().Uint32Value() : 4); auto docs = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), docs.size()); for (size_t i = 0; i < docs.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), docs[i])); return out; }

Napi::Object LlamaIndexAdapterWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "LlamaIndexAdapter", {
        InstanceMethod("buildIndex", &LlamaIndexAdapterWrap::BuildIndex),
        InstanceMethod("query", &LlamaIndexAdapterWrap::Query),
        InstanceMethod("insert", &LlamaIndexAdapterWrap::Insert),
        InstanceMethod("deleteDocument", &LlamaIndexAdapterWrap::DeleteDocument),
        InstanceMethod("save", &LlamaIndexAdapterWrap::Save),
        StaticMethod("load", &LlamaIndexAdapterWrap::Load),
    });
    g_llamaindex_constructor = Napi::Persistent(func);
    g_llamaindex_constructor.SuppressDestruct();
    exports.Set("LlamaIndexAdapter", func);
    return exports;
}

LlamaIndexAdapterWrap::LlamaIndexAdapterWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<LlamaIndexAdapterWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::framework::LlamaIndexAdapter>>>();
        adapter_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    vdb::framework::LlamaIndexConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto options = info[0].As<Napi::Object>();
        if (options.Has("indexType")) config.index_type = options.Get("indexType").ToString().Utf8Value();
        if (options.Has("chunkSize")) config.chunk_size = options.Get("chunkSize").As<Napi::Number>().Int64Value();
        if (options.Has("chunkOverlap")) config.chunk_overlap = options.Get("chunkOverlap").As<Napi::Number>().Int64Value();
        if (options.Has("includeMetadata")) config.include_metadata = options.Get("includeMetadata").As<Napi::Boolean>().Value();
    }
    adapter_ = std::make_unique<vdb::framework::LlamaIndexAdapter>(config);
}

Napi::Value LlamaIndexAdapterWrap::BuildIndex(const Napi::CallbackInfo& info) { auto result = adapter_->build_index(StringVector(info[0].As<Napi::Array>()), info.Length() > 1 ? MetadataVector(info.Env(), info[1]) : std::vector<vdb::Metadata>{}); UnwrapResult(result, info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value LlamaIndexAdapterWrap::Query(const Napi::CallbackInfo& info) { auto result = adapter_->query(info[0].ToString().Utf8Value(), info.Length() > 1 ? info[1].As<Napi::Number>().Uint32Value() : 5); auto context = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); return RAGContextToJs(info.Env(), context); }
Napi::Value LlamaIndexAdapterWrap::Insert(const Napi::CallbackInfo& info) { auto result = adapter_->insert(info[0].ToString().Utf8Value(), info.Length() > 1 && info[1].IsObject() ? JSToMetadata(info[1].As<Napi::Object>()) : vdb::Metadata{}); UnwrapResult(result, info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value LlamaIndexAdapterWrap::DeleteDocument(const Napi::CallbackInfo& info) { auto result = adapter_->delete_document(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())); UnwrapResult(result, info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value LlamaIndexAdapterWrap::Save(const Napi::CallbackInfo& info) { auto result = adapter_->save(info[0].As<Napi::String>().Utf8Value()); UnwrapResult(result, info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value LlamaIndexAdapterWrap::Load(const Napi::CallbackInfo& info) { Napi::Env env = info.Env(); Napi::Object options = Napi::Object::New(env); if (info.Length() > 0 && info[0].IsString()) options.Set("indexType", Napi::String::New(env, std::filesystem::path(info[0].As<Napi::String>().Utf8Value()).stem().string())); return g_llamaindex_constructor.New({options}); }

Napi::Object DocumentChunkerWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "DocumentChunker", {
        InstanceMethod("chunk", &DocumentChunkerWrap::Chunk),
        InstanceMethod("chunkWithMetadata", &DocumentChunkerWrap::ChunkWithMetadata),
    });
    g_chunker_constructor = Napi::Persistent(func);
    g_chunker_constructor.SuppressDestruct();
    exports.Set("DocumentChunker", func);
    return exports;
}

DocumentChunkerWrap::DocumentChunkerWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<DocumentChunkerWrap>(info) {
    vdb::framework::ChunkingConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        auto options = info[0].As<Napi::Object>();
        if (options.Has("strategy")) {
            const auto strategy = options.Get("strategy").ToString().Utf8Value();
            if (strategy == "fixed") config.strategy = vdb::framework::ChunkingStrategy::Fixed;
            else if (strategy == "paragraph") config.strategy = vdb::framework::ChunkingStrategy::Paragraph;
            else if (strategy == "semantic") config.strategy = vdb::framework::ChunkingStrategy::Semantic;
            else if (strategy == "recursive") config.strategy = vdb::framework::ChunkingStrategy::Recursive;
            else config.strategy = vdb::framework::ChunkingStrategy::Sentence;
        }
        if (options.Has("chunkSize")) config.chunk_size = options.Get("chunkSize").As<Napi::Number>().Int64Value();
        if (options.Has("chunkOverlap")) config.chunk_overlap = options.Get("chunkOverlap").As<Napi::Number>().Int64Value();
        if (options.Has("separator")) config.separator = options.Get("separator").ToString().Utf8Value();
        if (options.Has("keepSeparator")) config.keep_separator = options.Get("keepSeparator").As<Napi::Boolean>().Value();
    }
    chunker_ = std::make_unique<vdb::framework::DocumentChunker>(config);
}

Napi::Value DocumentChunkerWrap::Chunk(const Napi::CallbackInfo& info) { auto result = chunker_->chunk(info[0].ToString().Utf8Value()); auto chunks = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), chunks.size()); for (size_t i = 0; i < chunks.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), chunks[i])); return out; }
Napi::Value DocumentChunkerWrap::ChunkWithMetadata(const Napi::CallbackInfo& info) { auto result = chunker_->chunk_with_metadata(info[0].ToString().Utf8Value(), info.Length() > 1 && info[1].IsObject() ? JSToMetadata(info[1].As<Napi::Object>()) : vdb::Metadata{}); auto chunks = UnwrapResult(result, info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), chunks.size()); for (size_t i = 0; i < chunks.size(); ++i) { Napi::Object item = Napi::Object::New(info.Env()); item.Set("text", Napi::String::New(info.Env(), chunks[i].text)); item.Set("startChar", Napi::Number::New(info.Env(), static_cast<double>(chunks[i].start_char))); item.Set("endChar", Napi::Number::New(info.Env(), static_cast<double>(chunks[i].end_char))); item.Set("metadata", MetadataToJS(info.Env(), chunks[i].metadata)); out.Set(static_cast<uint32_t>(i), item);} return out; }

void RAGUtils::Init(Napi::Env env, Napi::Object& exports) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("findGgufModels", Napi::Function::New(env, FindGgufModels));
    obj.Set("readGgufMetadata", Napi::Function::New(env, ReadGgufMetadata));
    obj.Set("applyChatTemplate", Napi::Function::New(env, ApplyChatTemplate));
    exports.Set("RAGUtils", obj);
}

Napi::Value RAGUtils::FindGgufModels(const Napi::CallbackInfo& info) { auto models = vdb::llm::find_gguf_models(info[0].As<Napi::String>().Utf8Value()); Napi::Array out = Napi::Array::New(info.Env(), models.size()); for (size_t i = 0; i < models.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), models[i].string())); return out; }
Napi::Value RAGUtils::ReadGgufMetadata(const Napi::CallbackInfo& info) { auto metadata = vdb::llm::read_gguf_metadata(info[0].As<Napi::String>().Utf8Value()); if (!metadata) return info.Env().Null(); Napi::Object obj = Napi::Object::New(info.Env()); obj.Set("name", Napi::String::New(info.Env(), metadata->name)); obj.Set("architecture", Napi::String::New(info.Env(), metadata->architecture)); obj.Set("contextLength", Napi::Number::New(info.Env(), metadata->context_length)); obj.Set("embeddingLength", Napi::Number::New(info.Env(), metadata->embedding_length)); obj.Set("vocabSize", Napi::Number::New(info.Env(), metadata->vocab_size)); obj.Set("quantization", Napi::String::New(info.Env(), metadata->quantization)); obj.Set("fileSize", Napi::Number::New(info.Env(), static_cast<double>(metadata->file_size))); return obj; }
Napi::Value RAGUtils::ApplyChatTemplate(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), vdb::llm::apply_chat_template(Messages(info[0].As<Napi::Array>()), info.Length() > 1 ? info[1].ToString().Utf8Value() : "chatml")); }

} // namespace hektor_native
