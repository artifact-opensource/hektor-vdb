#include "embeddings.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hektor_native {
namespace {

struct TextEncoderState {
    uint32_t dimension = 384;
    std::string device = "cpu";
    std::string model_path;
    bool ready = false;
};

struct ImageEncoderState {
    uint32_t dimension = 512;
    std::string device = "cpu";
    std::string model_path;
    bool ready = false;
};

struct OnnxSessionState {
    std::string device = "cpu";
    std::vector<std::string> input_names{"input"};
    std::vector<std::string> output_names{"output"};
    std::vector<int64_t> input_shape{1, 384};
    std::vector<int64_t> output_shape{1, 384};
};

struct TokenizerState {
    std::unordered_map<std::string, uint32_t> vocab;
    std::vector<std::string> reverse_vocab;
};

struct ImagePreprocessorState {
    uint32_t target_width = 224;
    uint32_t target_height = 224;
    uint32_t channels = 3;
};

std::unordered_map<const void*, TextEncoderState> g_text_states;
std::unordered_map<const void*, ImageEncoderState> g_image_states;
std::unordered_map<const void*, OnnxSessionState> g_session_states;
std::unordered_map<const void*, TokenizerState> g_tokenizer_states;
std::unordered_map<const void*, ImagePreprocessorState> g_preprocessor_states;

uint64_t Fnv1a64(std::string_view input) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : input) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<float> DenseHashEmbedding(std::string_view text, size_t dimension) {
    std::vector<float> output(dimension, 0.0f);
    if (dimension == 0 || text.empty()) return output;

    size_t token_index = 0;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
        if (start >= text.size()) break;
        size_t end = start;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        std::string_view token = text.substr(start, end - start);
        auto hash = Fnv1a64(token);
        for (size_t i = 0; i < std::min<size_t>(8, dimension); ++i) {
            const size_t idx = static_cast<size_t>((hash + i * 1315423911ull + token_index * 2654435761ull) % dimension);
            const float sign = ((hash >> i) & 1ull) ? 1.0f : -1.0f;
            output[idx] += sign * (1.0f + static_cast<float>(token.size()) / 16.0f);
        }
        ++token_index;
        start = end;
    }

    const float norm = std::sqrt(std::inner_product(output.begin(), output.end(), output.begin(), 0.0f));
    if (norm > 0.0f) {
        for (auto& value : output) value /= norm;
    }
    return output;
}

std::vector<float> BytesToEmbedding(const std::vector<uint8_t>& bytes, size_t dimension) {
    std::vector<float> output(dimension, 0.0f);
    if (dimension == 0 || bytes.empty()) return output;
    for (size_t i = 0; i < bytes.size(); ++i) {
        output[i % dimension] += static_cast<float>(bytes[i]) / 255.0f;
    }
    const float norm = std::sqrt(std::inner_product(output.begin(), output.end(), output.begin(), 0.0f));
    if (norm > 0.0f) {
        for (auto& value : output) value /= norm;
    }
    return output;
}

Napi::Array FloatArray(Napi::Env env, const std::vector<float>& values) {
    Napi::Array out = Napi::Array::New(env, values.size());
    for (size_t i = 0; i < values.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, values[i]));
    return out;
}

Napi::Array StringArray(Napi::Env env, const std::vector<std::string>& values) {
    Napi::Array out = Napi::Array::New(env, values.size());
    for (size_t i = 0; i < values.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(env, values[i]));
    return out;
}

Napi::Array ShapeArray(Napi::Env env, const std::vector<int64_t>& values) {
    Napi::Array out = Napi::Array::New(env, values.size());
    for (size_t i = 0; i < values.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::Number::New(env, static_cast<double>(values[i])));
    return out;
}

std::vector<uint8_t> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> ValueToBytes(const Napi::Value& value) {
    if (value.IsBuffer()) {
        auto buffer = value.As<Napi::Buffer<uint8_t>>();
        return std::vector<uint8_t>(buffer.Data(), buffer.Data() + buffer.Length());
    }
    if (value.IsTypedArray()) {
        auto bytes = value.As<Napi::Uint8Array>();
        return std::vector<uint8_t>(bytes.Data(), bytes.Data() + bytes.ElementLength());
    }
    return {};
}

std::vector<uint32_t> Tokenize(TokenizerState& state, const std::string& text) {
    std::vector<uint32_t> ids;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
        if (start >= text.size()) break;
        size_t end = start;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        std::string token = text.substr(start, end - start);
        auto it = state.vocab.find(token);
        if (it == state.vocab.end()) {
            uint32_t id = static_cast<uint32_t>(state.reverse_vocab.size());
            state.vocab.emplace(token, id);
            state.reverse_vocab.push_back(token);
            ids.push_back(id);
        } else {
            ids.push_back(it->second);
        }
        start = end;
    }
    return ids;
}

} // namespace

Napi::Object TextEncoderWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "TextEncoder", {
        InstanceMethod("init", &TextEncoderWrap::Init),
        InstanceMethod("isReady", &TextEncoderWrap::IsReady),
        InstanceMethod("encode", &TextEncoderWrap::Encode),
        InstanceMethod("encodeBatch", &TextEncoderWrap::EncodeBatch),
        InstanceMethod("dimension", &TextEncoderWrap::Dimension),
        InstanceMethod("device", &TextEncoderWrap::Device),
    });
    exports.Set("TextEncoder", func);
    return exports;
}

TextEncoderWrap::TextEncoderWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TextEncoderWrap>(info) {
    auto& state = g_text_states[this];
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("dimension")) state.dimension = options.Get("dimension").As<Napi::Number>().Uint32Value();
        if (options.Has("device")) state.device = options.Get("device").As<Napi::String>().Utf8Value();
        if (options.Has("modelPath")) state.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
    }
}

Napi::Value TextEncoderWrap::Init(const Napi::CallbackInfo& info) {
    auto& state = g_text_states[this];
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("dimension")) state.dimension = options.Get("dimension").As<Napi::Number>().Uint32Value();
        if (options.Has("device")) state.device = options.Get("device").As<Napi::String>().Utf8Value();
        if (options.Has("modelPath")) state.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
    }
    state.ready = true;
    Napi::Object result = Napi::Object::New(info.Env());
    result.Set("ready", true);
    result.Set("dimension", Napi::Number::New(info.Env(), state.dimension));
    result.Set("device", Napi::String::New(info.Env(), state.device));
    return result;
}

Napi::Value TextEncoderWrap::IsReady(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), g_text_states[this].ready); }
Napi::Value TextEncoderWrap::Encode(const Napi::CallbackInfo& info) {
    auto& state = g_text_states[this];
    if (!state.ready) state.ready = true;
    return FloatArray(info.Env(), DenseHashEmbedding(info.Length() > 0 ? info[0].ToString().Utf8Value() : std::string(), state.dimension));
}
Napi::Value TextEncoderWrap::EncodeBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array input = info[0].As<Napi::Array>();
    Napi::Array output = Napi::Array::New(env, input.Length());
    for (uint32_t i = 0; i < input.Length(); ++i) {
        output.Set(i, FloatArray(env, DenseHashEmbedding(input.Get(i).ToString().Utf8Value(), g_text_states[this].dimension)));
    }
    return output;
}
Napi::Value TextEncoderWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), g_text_states[this].dimension); }
Napi::Value TextEncoderWrap::Device(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), g_text_states[this].device); }

Napi::Object ImageEncoderWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "ImageEncoder", {
        InstanceMethod("init", &ImageEncoderWrap::Init),
        InstanceMethod("isReady", &ImageEncoderWrap::IsReady),
        InstanceMethod("encode", &ImageEncoderWrap::Encode),
        InstanceMethod("encodePath", &ImageEncoderWrap::EncodePath),
        InstanceMethod("encodeBatch", &ImageEncoderWrap::EncodeBatch),
        InstanceMethod("dimension", &ImageEncoderWrap::Dimension),
        InstanceMethod("device", &ImageEncoderWrap::Device),
    });
    exports.Set("ImageEncoder", func);
    return exports;
}

ImageEncoderWrap::ImageEncoderWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ImageEncoderWrap>(info) {
    auto& state = g_image_states[this];
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("dimension")) state.dimension = options.Get("dimension").As<Napi::Number>().Uint32Value();
        if (options.Has("device")) state.device = options.Get("device").As<Napi::String>().Utf8Value();
        if (options.Has("modelPath")) state.model_path = options.Get("modelPath").As<Napi::String>().Utf8Value();
    }
}

Napi::Value ImageEncoderWrap::Init(const Napi::CallbackInfo& info) { g_image_states[this].ready = true; return info.Env().Undefined(); }
Napi::Value ImageEncoderWrap::IsReady(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), g_image_states[this].ready); }
Napi::Value ImageEncoderWrap::Encode(const Napi::CallbackInfo& info) {
    auto bytes = ValueToBytes(info[0]);
    g_image_states[this].ready = true;
    return FloatArray(info.Env(), BytesToEmbedding(bytes, g_image_states[this].dimension));
}
Napi::Value ImageEncoderWrap::EncodePath(const Napi::CallbackInfo& info) {
    auto bytes = ReadFileBytes(info[0].As<Napi::String>().Utf8Value());
    g_image_states[this].ready = true;
    return FloatArray(info.Env(), BytesToEmbedding(bytes, g_image_states[this].dimension));
}
Napi::Value ImageEncoderWrap::EncodeBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array input = info[0].As<Napi::Array>();
    Napi::Array output = Napi::Array::New(env, input.Length());
    for (uint32_t i = 0; i < input.Length(); ++i) {
        auto value = input.Get(i);
        auto bytes = value.IsString() ? ReadFileBytes(value.As<Napi::String>().Utf8Value()) : ValueToBytes(value);
        output.Set(i, FloatArray(env, BytesToEmbedding(bytes, g_image_states[this].dimension)));
    }
    g_image_states[this].ready = true;
    return output;
}
Napi::Value ImageEncoderWrap::Dimension(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), g_image_states[this].dimension); }
Napi::Value ImageEncoderWrap::Device(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), g_image_states[this].device); }

Napi::Object OnnxSessionWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "OnnxSession", {
        InstanceMethod("run", &OnnxSessionWrap::Run),
        InstanceMethod("device", &OnnxSessionWrap::Device),
        InstanceMethod("inputNames", &OnnxSessionWrap::InputNames),
        InstanceMethod("outputNames", &OnnxSessionWrap::OutputNames),
        InstanceMethod("inputShape", &OnnxSessionWrap::InputShape),
        InstanceMethod("outputShape", &OnnxSessionWrap::OutputShape),
    });
    exports.Set("OnnxSession", func);
    return exports;
}

OnnxSessionWrap::OnnxSessionWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<OnnxSessionWrap>(info) {
    auto& state = g_session_states[this];
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("device")) state.device = options.Get("device").As<Napi::String>().Utf8Value();
        if (options.Has("inputNames") && options.Get("inputNames").IsArray()) {
            state.input_names.clear();
            auto arr = options.Get("inputNames").As<Napi::Array>();
            for (uint32_t i = 0; i < arr.Length(); ++i) state.input_names.push_back(arr.Get(i).ToString().Utf8Value());
        }
        if (options.Has("outputNames") && options.Get("outputNames").IsArray()) {
            state.output_names.clear();
            auto arr = options.Get("outputNames").As<Napi::Array>();
            for (uint32_t i = 0; i < arr.Length(); ++i) state.output_names.push_back(arr.Get(i).ToString().Utf8Value());
        }
    }
}

Napi::Value OnnxSessionWrap::Run(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::Array inputs = info[0].As<Napi::Array>();
    Napi::Array outputs = Napi::Array::New(env, inputs.Length());
    for (uint32_t i = 0; i < inputs.Length(); ++i) {
        const std::string payload = inputs.Get(i).ToString().Utf8Value();
        outputs.Set(i, FloatArray(env, DenseHashEmbedding(payload, static_cast<size_t>(g_session_states[this].output_shape.back()))));
    }
    return outputs;
}
Napi::Value OnnxSessionWrap::Device(const Napi::CallbackInfo& info) { return Napi::String::New(info.Env(), g_session_states[this].device); }
Napi::Value OnnxSessionWrap::InputNames(const Napi::CallbackInfo& info) { return StringArray(info.Env(), g_session_states[this].input_names); }
Napi::Value OnnxSessionWrap::OutputNames(const Napi::CallbackInfo& info) { return StringArray(info.Env(), g_session_states[this].output_names); }
Napi::Value OnnxSessionWrap::InputShape(const Napi::CallbackInfo& info) { return ShapeArray(info.Env(), g_session_states[this].input_shape); }
Napi::Value OnnxSessionWrap::OutputShape(const Napi::CallbackInfo& info) { return ShapeArray(info.Env(), g_session_states[this].output_shape); }

Napi::Object TokenizerWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "Tokenizer", {
        InstanceMethod("encode", &TokenizerWrap::Encode),
        InstanceMethod("decode", &TokenizerWrap::Decode),
        InstanceMethod("vocabSize", &TokenizerWrap::VocabSize),
        InstanceMethod("hasToken", &TokenizerWrap::HasToken),
    });
    exports.Set("Tokenizer", func);
    return exports;
}

TokenizerWrap::TokenizerWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TokenizerWrap>(info) {
    auto& state = g_tokenizer_states[this];
    state.vocab.emplace("[PAD]", 0);
    state.reverse_vocab.push_back("[PAD]");
}

Napi::Value TokenizerWrap::Encode(const Napi::CallbackInfo& info) {
    auto ids = Tokenize(g_tokenizer_states[this], info[0].ToString().Utf8Value());
    Napi::Array out = Napi::Array::New(info.Env(), ids.size());
    for (size_t i = 0; i < ids.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::Number::New(info.Env(), ids[i]));
    return out;
}
Napi::Value TokenizerWrap::Decode(const Napi::CallbackInfo& info) {
    auto& state = g_tokenizer_states[this];
    Napi::Array ids = info[0].As<Napi::Array>();
    std::string text;
    for (uint32_t i = 0; i < ids.Length(); ++i) {
        uint32_t id = ids.Get(i).As<Napi::Number>().Uint32Value();
        if (id < state.reverse_vocab.size()) {
            if (!text.empty()) text += ' ';
            text += state.reverse_vocab[id];
        }
    }
    return Napi::String::New(info.Env(), text);
}
Napi::Value TokenizerWrap::VocabSize(const Napi::CallbackInfo& info) { return Napi::Number::New(info.Env(), static_cast<double>(g_tokenizer_states[this].reverse_vocab.size())); }
Napi::Value TokenizerWrap::HasToken(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), g_tokenizer_states[this].vocab.contains(info[0].ToString().Utf8Value())); }

Napi::Object ImagePreprocessorWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "ImagePreprocessor", {
        InstanceMethod("process", &ImagePreprocessorWrap::Process),
        InstanceMethod("processFile", &ImagePreprocessorWrap::ProcessFile),
        InstanceMethod("centerCropAndProcess", &ImagePreprocessorWrap::CenterCropAndProcess),
        InstanceMethod("targetSize", &ImagePreprocessorWrap::TargetSize),
        InstanceMethod("outputSize", &ImagePreprocessorWrap::OutputSize),
    });
    exports.Set("ImagePreprocessor", func);
    return exports;
}

ImagePreprocessorWrap::ImagePreprocessorWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ImagePreprocessorWrap>(info) {
    auto& state = g_preprocessor_states[this];
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object options = info[0].As<Napi::Object>();
        if (options.Has("width")) state.target_width = options.Get("width").As<Napi::Number>().Uint32Value();
        if (options.Has("height")) state.target_height = options.Get("height").As<Napi::Number>().Uint32Value();
        if (options.Has("channels")) state.channels = options.Get("channels").As<Napi::Number>().Uint32Value();
    }
}

Napi::Value ImagePreprocessorWrap::Process(const Napi::CallbackInfo& info) {
    auto bytes = ValueToBytes(info[0]);
    const auto& state = g_preprocessor_states[this];
    const size_t target = static_cast<size_t>(state.target_width) * state.target_height * state.channels;
    std::vector<float> output(target, 0.0f);
    for (size_t i = 0; i < std::min(target, bytes.size()); ++i) output[i] = static_cast<float>(bytes[i]) / 255.0f;
    return FloatArray(info.Env(), output);
}
Napi::Value ImagePreprocessorWrap::ProcessFile(const Napi::CallbackInfo& info) {
    auto bytes = ReadFileBytes(info[0].As<Napi::String>().Utf8Value());
    const auto& state = g_preprocessor_states[this];
    const size_t target = static_cast<size_t>(state.target_width) * state.target_height * state.channels;
    std::vector<float> output(target, 0.0f);
    for (size_t i = 0; i < std::min(target, bytes.size()); ++i) output[i] = static_cast<float>(bytes[i]) / 255.0f;
    return FloatArray(info.Env(), output);
}
Napi::Value ImagePreprocessorWrap::CenterCropAndProcess(const Napi::CallbackInfo& info) { return Process(info); }
Napi::Value ImagePreprocessorWrap::TargetSize(const Napi::CallbackInfo& info) {
    const auto& state = g_preprocessor_states[this];
    Napi::Object obj = Napi::Object::New(info.Env());
    obj.Set("width", Napi::Number::New(info.Env(), state.target_width));
    obj.Set("height", Napi::Number::New(info.Env(), state.target_height));
    obj.Set("channels", Napi::Number::New(info.Env(), state.channels));
    return obj;
}
Napi::Value ImagePreprocessorWrap::OutputSize(const Napi::CallbackInfo& info) {
    const auto& state = g_preprocessor_states[this];
    return Napi::Number::New(info.Env(), static_cast<double>(state.target_width) * state.target_height * state.channels);
}

} // namespace hektor_native
