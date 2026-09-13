#include "framework.h"

#include "database.h"
#include "vdb/framework_integration.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace hektor_native {
namespace {

Napi::FunctionReference g_tf_constructor;
Napi::FunctionReference g_torch_constructor;

Napi::Array VectorArray(Napi::Env env, const vdb::Vector& vector) {
    Napi::Array out = Napi::Array::New(env, vector.size());
    for (size_t i = 0; i < vector.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, vector[i]));
    return out;
}

Napi::Array VectorBatchArray(Napi::Env env, const std::vector<vdb::Vector>& vectors) {
    Napi::Array out = Napi::Array::New(env, vectors.size());
    for (size_t i = 0; i < vectors.size(); ++i) out.Set(static_cast<uint32_t>(i), VectorArray(env, vectors[i]));
    return out;
}

std::vector<std::string> StringVector(const Napi::Array& arr) {
    std::vector<std::string> out;
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) out.push_back(arr.Get(i).ToString().Utf8Value());
    return out;
}

std::vector<vdb::Vector> VectorMatrix(const Napi::Array& arr) {
    std::vector<vdb::Vector> out;
    out.reserve(arr.Length());
    for (uint32_t i = 0; i < arr.Length(); ++i) out.emplace_back(JSToVector(arr.Get(i).As<Napi::Array>()));
    return out;
}

} // namespace

Napi::Object TensorFlowEmbedderWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "TensorFlowEmbedder", {
        InstanceMethod("embed", &TensorFlowEmbedderWrap::Embed),
        InstanceMethod("embedBatch", &TensorFlowEmbedderWrap::EmbedBatch),
        InstanceMethod("dimension", &TensorFlowEmbedderWrap::Dimension),
        InstanceMethod("isLoaded", &TensorFlowEmbedderWrap::IsLoaded),
        StaticMethod("exportForTraining", &TensorFlowEmbedderWrap::ExportForTraining),
    });
    g_tf_constructor = Napi::Persistent(func);
    g_tf_constructor.SuppressDestruct();
    exports.Set("TensorFlowEmbedder", func);
    return exports;
}

TensorFlowEmbedderWrap::TensorFlowEmbedderWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TensorFlowEmbedderWrap>(info) {
    vdb::framework::TensorFlowConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("modelPath")) config.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
        if (options.Has("inputTensorName")) config.input_tensor_name = options.Get("inputTensorName").As<Napi::String>().Utf8Value();
        if (options.Has("outputTensorName")) config.output_tensor_name = options.Get("outputTensorName").As<Napi::String>().Utf8Value();
        if (options.Has("numThreads")) config.num_threads = options.Get("numThreads").As<Napi::Number>().Int32Value();
        if (options.Has("useGpu")) config.use_gpu = options.Get("useGpu").As<Napi::Boolean>().Value();
    }
    embedder_ = std::make_unique<vdb::framework::TensorFlowEmbedder>(config);
}

Napi::Value TensorFlowEmbedderWrap::Embed(const Napi::CallbackInfo& info) {
    auto result = embedder_->embed(info[0].ToString().Utf8Value());
    auto vector = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    return VectorArray(info.Env(), vector);
}
Napi::Value TensorFlowEmbedderWrap::EmbedBatch(const Napi::CallbackInfo& info) {
    auto result = embedder_->embed_batch(StringVector(info[0].As<Napi::Array>()));
    auto vectors = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    return VectorBatchArray(info.Env(), vectors);
}
Napi::Value TensorFlowEmbedderWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), embedder_->dimension()); }
Napi::Value TensorFlowEmbedderWrap::IsLoaded(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), embedder_->is_loaded()); }
Napi::Value TensorFlowEmbedderWrap::ExportForTraining(const Napi::CallbackInfo& info) {
    auto vectors = VectorMatrix(info[0].As<Napi::Array>());
    auto labels = StringVector(info[1].As<Napi::Array>());
    auto path = info[2].As<Napi::String>().Utf8Value();
    auto result = vdb::framework::TensorFlowEmbedder::export_for_training(vectors, labels, path);
    UnwrapResult(result, info.Env());
    return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending());
}

Napi::Object PyTorchEmbedderWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "PyTorchEmbedder", {
        InstanceMethod("embed", &PyTorchEmbedderWrap::Embed),
        InstanceMethod("embedBatch", &PyTorchEmbedderWrap::EmbedBatch),
        InstanceMethod("dimension", &PyTorchEmbedderWrap::Dimension),
        InstanceMethod("isLoaded", &PyTorchEmbedderWrap::IsLoaded),
        InstanceMethod("device", &PyTorchEmbedderWrap::Device),
        StaticMethod("exportForTraining", &PyTorchEmbedderWrap::ExportForTraining),
        StaticMethod("fromTrained", &PyTorchEmbedderWrap::FromTrained),
    });
    g_torch_constructor = Napi::Persistent(func);
    g_torch_constructor.SuppressDestruct();
    exports.Set("PyTorchEmbedder", func);
    return exports;
}

PyTorchEmbedderWrap::PyTorchEmbedderWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<PyTorchEmbedderWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::framework::PyTorchEmbedder>>>();
        embedder_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    vdb::framework::PyTorchConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("modelPath")) config.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
        if (options.Has("device")) config.device = options.Get("device").As<Napi::String>().Utf8Value();
        if (options.Has("numThreads")) config.num_threads = options.Get("numThreads").As<Napi::Number>().Int32Value();
        if (options.Has("useHalfPrecision")) config.use_half_precision = options.Get("useHalfPrecision").As<Napi::Boolean>().Value();
    }
    embedder_ = std::make_unique<vdb::framework::PyTorchEmbedder>(config);
}

Napi::Value PyTorchEmbedderWrap::Embed(const Napi::CallbackInfo& info) {
    auto result = embedder_->embed(info[0].ToString().Utf8Value());
    auto vector = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    return VectorArray(info.Env(), vector);
}
Napi::Value PyTorchEmbedderWrap::EmbedBatch(const Napi::CallbackInfo& info) {
    auto result = embedder_->embed_batch(StringVector(info[0].As<Napi::Array>()));
    auto vectors = UnwrapResult(result, info.Env());
    if (info.Env().IsExceptionPending()) return info.Env().Null();
    return VectorBatchArray(info.Env(), vectors);
}
Napi::Value PyTorchEmbedderWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), embedder_->dimension()); }
Napi::Value PyTorchEmbedderWrap::IsLoaded(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), embedder_->is_loaded()); }
Napi::Value PyTorchEmbedderWrap::Device(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), embedder_->device()); }
Napi::Value PyTorchEmbedderWrap::ExportForTraining(const Napi::CallbackInfo& info) {
    auto vectors = VectorMatrix(info[0].As<Napi::Array>());
    auto labels = StringVector(info[1].As<Napi::Array>());
    auto path = info[2].As<Napi::String>().Utf8Value();
    auto result = vdb::framework::PyTorchEmbedder::export_for_training(vectors, labels, path);
    UnwrapResult(result, info.Env());
    return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending());
}
Napi::Value PyTorchEmbedderWrap::FromTrained(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Object options = Napi::Object::New(env);
    options.Set("modelPath", info[0].As<Napi::String>());
    options.Set("device", Napi::String::New(env, info.Length() > 1 ? info[1].ToString().Utf8Value() : "cpu"));
    return g_torch_constructor.New({options});
}

} // namespace hektor_native
