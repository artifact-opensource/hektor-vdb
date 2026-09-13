#include "telemetry.h"

#include "vdb/logging.hpp"
#include "vdb/telemetry.hpp"

#include <cctype>
#include <string>
#include <unordered_map>

namespace hektor_native {
namespace {

Napi::FunctionReference g_span_constructor;

std::unordered_map<std::string, std::string> JsLabels(const Napi::Value& value) {
    std::unordered_map<std::string, std::string> labels;
    if (!value.IsObject()) return labels;
    Napi::Object object = value.As<Napi::Object>();
    auto names = object.GetPropertyNames();
    for (uint32_t i = 0; i < names.Length(); ++i) {
        auto key = names.Get(i).ToString().Utf8Value();
        labels.emplace(key, object.Get(key).ToString().Utf8Value());
    }
    return labels;
}

vdb::telemetry::SpanAttributes JsSpanAttributes(const Napi::Object& object) {
    vdb::telemetry::SpanAttributes attrs;
    if (object.Has("operationName")) attrs.operation_name = object.Get("operationName").As<Napi::String>().Utf8Value();
    if (object.Has("operationType")) attrs.operation_type = object.Get("operationType").As<Napi::String>().Utf8Value();
    if (object.Has("vectorDimension")) attrs.vector_dimension = object.Get("vectorDimension").As<Napi::Number>().Int64Value();
    if (object.Has("vectorCount")) attrs.vector_count = object.Get("vectorCount").As<Napi::Number>().Int64Value();
    if (object.Has("resultCount")) attrs.result_count = object.Get("resultCount").As<Napi::Number>().Int64Value();
    if (object.Has("dbName")) attrs.db_name = object.Get("dbName").As<Napi::String>().Utf8Value();
    if (object.Has("collectionName")) attrs.collection_name = object.Get("collectionName").As<Napi::String>().Utf8Value();
    if (object.Has("queryLatencyMs")) attrs.query_latency_ms = object.Get("queryLatencyMs").As<Napi::Number>().DoubleValue();
    if (object.Has("memoryBytes")) attrs.memory_bytes = object.Get("memoryBytes").As<Napi::Number>().Int64Value();
    if (object.Has("custom")) attrs.custom = JsLabels(object.Get("custom"));
    return attrs;
}

vdb::logging::LogLevel ParseLevel(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (value == "debug") return vdb::logging::LogLevel::DEBUG;
    if (value == "warn") return vdb::logging::LogLevel::WARN;
    if (value == "error") return vdb::logging::LogLevel::ERROR;
    if (value == "critical") return vdb::logging::LogLevel::CRITICAL;
    return vdb::logging::LogLevel::INFO;
}

vdb::logging::AnomalyType ParseAnomalyType(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (value == "PARSE_ERROR") return vdb::logging::AnomalyType::PARSE_ERROR;
    if (value == "DATA_CORRUPTION") return vdb::logging::AnomalyType::DATA_CORRUPTION;
    if (value == "UNEXPECTED_SIZE") return vdb::logging::AnomalyType::UNEXPECTED_SIZE;
    if (value == "INVALID_ENCODING") return vdb::logging::AnomalyType::INVALID_ENCODING;
    if (value == "SQL_INJECTION_ATTEMPT") return vdb::logging::AnomalyType::SQL_INJECTION_ATTEMPT;
    if (value == "MEMORY_ANOMALY") return vdb::logging::AnomalyType::MEMORY_ANOMALY;
    if (value == "PERFORMANCE_DEGRADATION") return vdb::logging::AnomalyType::PERFORMANCE_DEGRADATION;
    if (value == "MISSING_DATA") return vdb::logging::AnomalyType::MISSING_DATA;
    if (value == "DUPLICATE_DATA") return vdb::logging::AnomalyType::DUPLICATE_DATA;
    if (value == "SCHEMA_MISMATCH") return vdb::logging::AnomalyType::SCHEMA_MISMATCH;
    if (value == "CONNECTION_FAILURE") return vdb::logging::AnomalyType::CONNECTION_FAILURE;
    if (value == "SECURITY_VIOLATION") return vdb::logging::AnomalyType::SECURITY_VIOLATION;
    if (value == "RESOURCE_EXHAUSTION") return vdb::logging::AnomalyType::RESOURCE_EXHAUSTION;
    if (value == "CUSTOM") return vdb::logging::AnomalyType::CUSTOM;
    return vdb::logging::AnomalyType::UNKNOWN_FORMAT;
}

vdb::telemetry::TelemetryConfig ParseTelemetryConfig(const Napi::Value& value) {
    vdb::telemetry::TelemetryConfig config;
    if (!value.IsObject()) return config;
    Napi::Object object = value.As<Napi::Object>();
    if (object.Has("enableTracing")) config.enable_tracing = object.Get("enableTracing").As<Napi::Boolean>().Value();
    if (object.Has("serviceName")) config.service_name = object.Get("serviceName").As<Napi::String>().Utf8Value();
    if (object.Has("serviceVersion")) config.service_version = object.Get("serviceVersion").As<Napi::String>().Utf8Value();
    if (object.Has("deploymentEnvironment")) config.deployment_environment = object.Get("deploymentEnvironment").As<Napi::String>().Utf8Value();
    if (object.Has("samplingRatio")) config.sampling_ratio = object.Get("samplingRatio").As<Napi::Number>().DoubleValue();
    if (object.Has("traceExporter")) config.trace_exporter = object.Get("traceExporter").As<Napi::String>().Utf8Value();
    if (object.Has("metricsExporter")) config.metrics_exporter = object.Get("metricsExporter").As<Napi::String>().Utf8Value();
    if (object.Has("otlpEndpoint")) config.otlp_endpoint = object.Get("otlpEndpoint").As<Napi::String>().Utf8Value();
    if (object.Has("otlpHeaders")) config.otlp_headers = object.Get("otlpHeaders").As<Napi::String>().Utf8Value();
    if (object.Has("prometheusHost")) config.prometheus_host = object.Get("prometheusHost").As<Napi::String>().Utf8Value();
    if (object.Has("prometheusPort")) config.prometheus_port = object.Get("prometheusPort").As<Napi::Number>().Int32Value();
    if (object.Has("resourceAttributes")) config.resource_attributes = JsLabels(object.Get("resourceAttributes"));
    return config;
}

} // namespace

Napi::Object TelemetrySpanWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "TelemetrySpan", {
        InstanceMethod("setAttribute", &TelemetrySpanWrap::SetAttribute),
        InstanceMethod("setAttributes", &TelemetrySpanWrap::SetAttributes),
        InstanceMethod("addEvent", &TelemetrySpanWrap::AddEvent),
        InstanceMethod("recordError", &TelemetrySpanWrap::RecordError),
        InstanceMethod("setStatus", &TelemetrySpanWrap::SetStatus),
        InstanceMethod("getContext", &TelemetrySpanWrap::GetContext),
    });
    g_span_constructor = Napi::Persistent(func);
    g_span_constructor.SuppressDestruct();
    exports.Set("TelemetrySpan", func);
    return exports;
}

TelemetrySpanWrap::TelemetrySpanWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TelemetrySpanWrap>(info) {
    if (info.Length() == 1 && info[0].IsExternal()) {
        auto external = info[0].As<Napi::External<std::unique_ptr<vdb::telemetry::TelemetrySpan>>>();
        span_ = std::move(*external.Data());
        delete external.Data();
        return;
    }
    const std::string operation = info.Length() > 0 && info[0].IsString() ? info[0].As<Napi::String>().Utf8Value() : "studio.operation";
    span_ = std::make_unique<vdb::telemetry::TelemetrySpan>(operation);
}

TelemetrySpanWrap::~TelemetrySpanWrap() = default;

Napi::Value TelemetrySpanWrap::SetAttribute(const Napi::CallbackInfo& info) {
    std::string key = info[0].As<Napi::String>().Utf8Value();
    if (info[1].IsBoolean()) span_->set_attribute(key, info[1].As<Napi::Boolean>().Value());
    else if (info[1].IsNumber()) span_->set_attribute(key, info[1].As<Napi::Number>().DoubleValue());
    else span_->set_attribute(key, info[1].ToString().Utf8Value());
    return info.This();
}

Napi::Value TelemetrySpanWrap::SetAttributes(const Napi::CallbackInfo& info) {
    span_->set_attributes(JsSpanAttributes(info[0].As<Napi::Object>()));
    return info.This();
}

Napi::Value TelemetrySpanWrap::AddEvent(const Napi::CallbackInfo& info) {
    const auto name = info[0].As<Napi::String>().Utf8Value();
    span_->add_event(name, info.Length() > 1 ? JsLabels(info[1]) : std::unordered_map<std::string, std::string>{});
    return info.This();
}

Napi::Value TelemetrySpanWrap::RecordError(const Napi::CallbackInfo& info) {
    span_->record_error(info[0].ToString().Utf8Value());
    return info.This();
}

Napi::Value TelemetrySpanWrap::SetStatus(const Napi::CallbackInfo& info) {
    const bool success = info[0].As<Napi::Boolean>().Value();
    const std::string description = info.Length() > 1 ? info[1].ToString().Utf8Value() : std::string();
    span_->set_status(success, description);
    return info.This();
}

Napi::Value TelemetrySpanWrap::GetContext(const Napi::CallbackInfo& info) {
    auto context = span_->get_context();
    Napi::Object obj = Napi::Object::New(info.Env());
    obj.Set("traceId", Napi::String::New(info.Env(), context.trace_id));
    obj.Set("spanId", Napi::String::New(info.Env(), context.span_id));
    obj.Set("parentSpanId", Napi::String::New(info.Env(), context.parent_span_id));
    obj.Set("sampled", Napi::Boolean::New(info.Env(), context.sampled));
    obj.Set("traceparent", Napi::String::New(info.Env(), context.to_w3c_traceparent()));
    return obj;
}

void TelemetryMetrics::Init(Napi::Env env, Napi::Object& exports) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("incrementCounter", Napi::Function::New(env, IncrementCounter));
    obj.Set("recordHistogram", Napi::Function::New(env, RecordHistogram));
    obj.Set("setGauge", Napi::Function::New(env, SetGauge));
    obj.Set("recordSearchLatency", Napi::Function::New(env, RecordSearchLatency));
    obj.Set("recordInsertOperation", Napi::Function::New(env, RecordInsertOperation));
    obj.Set("recordIndexBuildTime", Napi::Function::New(env, RecordIndexBuildTime));
    obj.Set("recordMemoryUsage", Napi::Function::New(env, RecordMemoryUsage));
    exports.Set("TelemetryMetrics", obj);
}

Napi::Value TelemetryMetrics::IncrementCounter(const Napi::CallbackInfo& info) {
    vdb::telemetry::TelemetryMetrics::instance().increment_counter(info[0].As<Napi::String>().Utf8Value(), info.Length() > 1 && info[1].IsNumber() ? info[1].As<Napi::Number>().Int64Value() : 1, info.Length() > 2 ? JsLabels(info[2]) : std::unordered_map<std::string, std::string>{});
    return info.Env().Undefined();
}
Napi::Value TelemetryMetrics::RecordHistogram(const Napi::CallbackInfo& info) {
    vdb::telemetry::TelemetryMetrics::instance().record_histogram(info[0].As<Napi::String>().Utf8Value(), info[1].As<Napi::Number>().DoubleValue(), info.Length() > 2 ? JsLabels(info[2]) : std::unordered_map<std::string, std::string>{});
    return info.Env().Undefined();
}
Napi::Value TelemetryMetrics::SetGauge(const Napi::CallbackInfo& info) {
    vdb::telemetry::TelemetryMetrics::instance().set_gauge(info[0].As<Napi::String>().Utf8Value(), info[1].As<Napi::Number>().DoubleValue(), info.Length() > 2 ? JsLabels(info[2]) : std::unordered_map<std::string, std::string>{});
    return info.Env().Undefined();
}
Napi::Value TelemetryMetrics::RecordSearchLatency(const Napi::CallbackInfo& info) { vdb::telemetry::TelemetryMetrics::instance().record_search_latency(info[0].As<Napi::Number>().DoubleValue(), info[1].As<Napi::Number>().Int64Value()); return info.Env().Undefined(); }
Napi::Value TelemetryMetrics::RecordInsertOperation(const Napi::CallbackInfo& info) { vdb::telemetry::TelemetryMetrics::instance().record_insert_operation(info[0].As<Napi::Number>().Int64Value(), info[1].As<Napi::Number>().DoubleValue()); return info.Env().Undefined(); }
Napi::Value TelemetryMetrics::RecordIndexBuildTime(const Napi::CallbackInfo& info) { vdb::telemetry::TelemetryMetrics::instance().record_index_build_time(info[0].As<Napi::Number>().DoubleValue()); return info.Env().Undefined(); }
Napi::Value TelemetryMetrics::RecordMemoryUsage(const Napi::CallbackInfo& info) { vdb::telemetry::TelemetryMetrics::instance().record_memory_usage(info[0].As<Napi::Number>().Int64Value()); return info.Env().Undefined(); }

void TelemetryManager::Init(Napi::Env env, Napi::Object& exports) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("initialize", Napi::Function::New(env, Initialize));
    obj.Set("shutdown", Napi::Function::New(env, Shutdown));
    obj.Set("isEnabled", Napi::Function::New(env, IsEnabled));
    obj.Set("startSpan", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
        Napi::Env env = info.Env();
        auto* holder = new std::unique_ptr<vdb::telemetry::TelemetrySpan>(std::make_unique<vdb::telemetry::TelemetrySpan>(info.Length() > 0 ? info[0].ToString().Utf8Value() : "studio.operation"));
        return g_span_constructor.New({Napi::External<std::unique_ptr<vdb::telemetry::TelemetrySpan>>::New(env, holder)});
    }));
    exports.Set("TelemetryManager", obj);
}

Napi::Value TelemetryManager::Initialize(const Napi::CallbackInfo& info) {
    vdb::telemetry::TelemetryManager::instance().initialize(ParseTelemetryConfig(info.Length() > 0 ? info[0] : info.Env().Undefined()));
    return info.Env().Undefined();
}
Napi::Value TelemetryManager::Shutdown(const Napi::CallbackInfo& info) { vdb::telemetry::TelemetryManager::instance().shutdown(); return info.Env().Undefined(); }
Napi::Value TelemetryManager::IsEnabled(const Napi::CallbackInfo& info) { return Napi::Boolean::New(info.Env(), vdb::telemetry::TelemetryManager::instance().is_enabled()); }

void Logger::Init(Napi::Env env, Napi::Object& exports) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("initialize", Napi::Function::New(env, Initialize));
    obj.Set("log", Napi::Function::New(env, Log));
    obj.Set("logAnomaly", Napi::Function::New(env, LogAnomaly));
    obj.Set("debug", Napi::Function::New(env, Debug));
    obj.Set("info", Napi::Function::New(env, Info));
    obj.Set("warn", Napi::Function::New(env, Warn));
    obj.Set("error", Napi::Function::New(env, Error));
    obj.Set("critical", Napi::Function::New(env, Critical));
    exports.Set("Logger", obj);
}

Napi::Value Logger::Initialize(const Napi::CallbackInfo& info) {
    vdb::logging::LoggerConfig config;
    if (info.Length() > 0 && info[0].IsObject()) {
        Napi::Object object = info[0].As<Napi::Object>();
        if (object.Has("minLevel")) config.min_level = ParseLevel(object.Get("minLevel").ToString().Utf8Value());
        if (object.Has("logToConsole")) config.log_to_console = object.Get("logToConsole").As<Napi::Boolean>().Value();
        if (object.Has("logToFile")) config.log_to_file = object.Get("logToFile").As<Napi::Boolean>().Value();
        if (object.Has("logFilePath")) config.log_file_path = object.Get("logFilePath").As<Napi::String>().Utf8Value();
        if (object.Has("anomalyLogPath")) config.anomaly_log_path = object.Get("anomalyLogPath").As<Napi::String>().Utf8Value();
    }
    vdb::logging::Logger::instance().initialize(config);
    return info.Env().Undefined();
}

Napi::Value Logger::Log(const Napi::CallbackInfo& info) {
    const auto level = ParseLevel(info[0].ToString().Utf8Value());
    const auto message = info[1].ToString().Utf8Value();
    vdb::logging::Logger::instance().log(level, message);
    return info.Env().Undefined();
}
Napi::Value Logger::LogAnomaly(const Napi::CallbackInfo& info) {
    vdb::logging::Logger::instance().log_anomaly(ParseAnomalyType(info[0].ToString().Utf8Value()), info[1].ToString().Utf8Value());
    return info.Env().Undefined();
}
Napi::Value Logger::Debug(const Napi::CallbackInfo& info) { vdb::logging::Logger::instance().debug(info[0].ToString().Utf8Value()); return info.Env().Undefined(); }
Napi::Value Logger::Info(const Napi::CallbackInfo& info) { vdb::logging::Logger::instance().info(info[0].ToString().Utf8Value()); return info.Env().Undefined(); }
Napi::Value Logger::Warn(const Napi::CallbackInfo& info) { vdb::logging::Logger::instance().warn(info[0].ToString().Utf8Value()); return info.Env().Undefined(); }
Napi::Value Logger::Error(const Napi::CallbackInfo& info) { vdb::logging::Logger::instance().error(info[0].ToString().Utf8Value()); return info.Env().Undefined(); }
Napi::Value Logger::Critical(const Napi::CallbackInfo& info) { vdb::logging::Logger::instance().critical(info[0].ToString().Utf8Value()); return info.Env().Undefined(); }

} // namespace hektor_native
