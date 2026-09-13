#include "ingestion.h"
#include "database.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hektor_native {

namespace {

using json = nlohmann::json;

vdb::Metadata MetadataFromJson(const json& value, const std::string& source_file) {
  vdb::Metadata meta;
  meta.source_file = source_file;
  if (value.contains("metadata") && value["metadata"].is_object()) {
    const auto& m = value["metadata"];
    if (m.contains("date") && m["date"].is_string()) meta.date = m["date"].get<std::string>();
    if (m.contains("source_file") && m["source_file"].is_string()) meta.source_file = m["source_file"].get<std::string>();
    if (m.contains("asset") && m["asset"].is_string()) meta.asset = m["asset"].get<std::string>();
    if (m.contains("bias") && m["bias"].is_string()) meta.bias = m["bias"].get<std::string>();
    if (m.contains("extra_json")) meta.extra_json = m["extra_json"].dump();
  }
  return meta;
}

std::string ExtractText(const json& value) {
  if (value.contains("text") && value["text"].is_string()) {
    return value["text"].get<std::string>();
  }
  if (value.contains("content") && value["content"].is_string()) {
    return value["content"].get<std::string>();
  }
  return value.is_string() ? value.get<std::string>() : value.dump();
}

}  // namespace

Napi::Object Ingestion::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "Ingestion", {
    InstanceMethod("ingestCSV", &Ingestion::IngestCSV),
    InstanceMethod("ingestCSVAsync", &Ingestion::IngestCSVAsync),
    InstanceMethod("ingestJSON", &Ingestion::IngestJSON),
    InstanceMethod("ingestJSONAsync", &Ingestion::IngestJSONAsync),
    InstanceMethod("ingestParquet", &Ingestion::IngestParquet),
    InstanceMethod("ingestParquetAsync", &Ingestion::IngestParquetAsync),
    InstanceMethod("batchIngest", &Ingestion::BatchIngest),
    InstanceMethod("getProgress", &Ingestion::GetProgress),
  });
  
  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  
  exports.Set("Ingestion", func);
  return exports;
}

Ingestion::Ingestion(const Napi::CallbackInfo& info) 
    : Napi::ObjectWrap<Ingestion>(info),
      total_items_(0),
      processed_items_(0),
      is_running_(false) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected database ID").ThrowAsJavaScriptException();
    return;
  }
  
  db_id_ = info[0].As<Napi::String>().Utf8Value();
  db_ = DatabaseManager::get(db_id_);
  
  if (!db_) {
    Napi::Error::New(env, "Database not found").ThrowAsJavaScriptException();
  }
}

Napi::Value Ingestion::IngestCSV(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (!db_ || !db_->is_ready()) {
    Napi::Error::New(env, "Database not ready").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected CSV file path").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  std::string filepath = info[0].As<Napi::String>().Utf8Value();
  
  // Parse CSV options
  std::string text_column = "text";
  if (info.Length() >= 2 && info[1].IsObject()) {
    Napi::Object opts = info[1].As<Napi::Object>();
    if (opts.Has("textColumn")) {
      text_column = opts.Get("textColumn").As<Napi::String>().Utf8Value();
    }
  }
  
  try {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      Napi::Object result = Napi::Object::New(env);
      result.Set("success", false);
      result.Set("error", "Failed to open file");
      return result;
    }
    
    std::string line;
    std::vector<std::string> headers;
    size_t count = 0;
    
    // Read header
    if (std::getline(file, line)) {
      std::stringstream ss(line);
      std::string cell;
      while (std::getline(ss, cell, ',')) {
        headers.push_back(cell);
      }
    }
    
    // Find text column index
    int text_idx = -1;
    for (size_t i = 0; i < headers.size(); i++) {
      if (headers[i] == text_column) {
        text_idx = i;
        break;
      }
    }
    
    if (text_idx == -1) {
      Napi::Object result = Napi::Object::New(env);
      result.Set("success", false);
      result.Set("error", "Text column not found");
      return result;
    }
    
    // Process rows
    while (std::getline(file, line)) {
      std::stringstream ss(line);
      std::string cell;
      std::vector<std::string> cells;
      
      while (std::getline(ss, cell, ',')) {
        cells.push_back(cell);
      }
      
      if (text_idx < static_cast<int>(cells.size())) {
        vdb::Metadata meta;
        meta.source_file = filepath;
        meta.type = vdb::DocumentType::Journal;  // Default type
        
        auto result = db_->add_text(cells[text_idx], meta);
        if (result.has_value()) {
          count++;
        }
      }
    }
    
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", true);
    result.Set("count", Napi::Number::New(env, count));
    result.Set("filepath", filepath);
    
    return result;
    
  } catch (const std::exception& e) {
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", false);
    result.Set("error", e.what());
    return result;
  }
}

Napi::Value Ingestion::IngestCSVAsync(const Napi::CallbackInfo& info) {
  return IngestCSV(info);
}

Napi::Value Ingestion::IngestJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!db_ || !db_->is_ready()) {
    Napi::Error::New(env, "Database not ready").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected JSON file path").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string filepath = info[0].As<Napi::String>().Utf8Value();
  try {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      Napi::Object result = Napi::Object::New(env);
      result.Set("success", false);
      result.Set("error", "Failed to open file");
      return result;
    }

    json parsed;
    file >> parsed;

    std::vector<json> documents;
    if (parsed.is_array()) {
      documents.assign(parsed.begin(), parsed.end());
    } else if (parsed.is_object() && parsed.contains("documents") && parsed["documents"].is_array()) {
      documents.assign(parsed["documents"].begin(), parsed["documents"].end());
    } else {
      documents.push_back(parsed);
    }

    size_t count = 0;
    std::vector<uint64_t> ids;
    for (const auto& doc : documents) {
      const auto text = ExtractText(doc);
      if (text.empty()) continue;
      auto result = db_->add_text(text, MetadataFromJson(doc, filepath));
      if (result.has_value()) {
        ids.push_back(result.value());
        ++count;
      }
    }

    Napi::Array ids_arr = Napi::Array::New(env, ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
      ids_arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, ids[i]));
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("success", true);
    result.Set("count", Napi::Number::New(env, count));
    result.Set("ids", ids_arr);
    result.Set("filepath", filepath);
    return result;
  } catch (const std::exception& e) {
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", false);
    result.Set("error", e.what());
    return result;
  }
}

Napi::Value Ingestion::IngestJSONAsync(const Napi::CallbackInfo& info) {
  return IngestJSON(info);
}

Napi::Value Ingestion::IngestParquet(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!db_ || !db_->is_ready()) {
    Napi::Error::New(env, "Database not ready").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected Parquet file path").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string filepath = info[0].As<Napi::String>().Utf8Value();
  const std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "hektor_parquet_ingest.jsonl";
  const std::string command =
      "python3 -c \"import json,sys; path=sys.argv[1]; out=sys.argv[2]; "
      "rows=None; "
      "try:\\n import pyarrow.parquet as pq; rows=pq.read_table(path).to_pylist()\\n"
      "except Exception:\\n import pandas as pd; rows=pd.read_parquet(path).to_dict(orient='records')\\n"
      "fh=open(out,'w',encoding='utf-8'); [fh.write(json.dumps(row)+'\\\\n') for row in rows]; fh.close()\" \"" +
      filepath + "\" \"" + temp_path.string() + "\"";

  const int rc = std::system(command.c_str());
  if (rc != 0) {
    Napi::Object result = Napi::Object::New(env);
    result.Set("success", false);
    result.Set("error", "Parquet ingestion requires pyarrow or pandas in the local Python environment");
    return result;
  }

  std::ifstream file(temp_path);
  std::string line;
  size_t count = 0;
  std::vector<uint64_t> ids;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    auto row = json::parse(line);
    const auto text = ExtractText(row);
    if (text.empty()) continue;
    auto result = db_->add_text(text, MetadataFromJson(row, filepath));
    if (result.has_value()) {
      ids.push_back(result.value());
      ++count;
    }
  }

  std::remove(temp_path.c_str());

  Napi::Array ids_arr = Napi::Array::New(env, ids.size());
  for (size_t i = 0; i < ids.size(); ++i) {
    ids_arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, ids[i]));
  }

  Napi::Object result = Napi::Object::New(env);
  result.Set("success", true);
  result.Set("count", Napi::Number::New(env, count));
  result.Set("ids", ids_arr);
  result.Set("filepath", filepath);
  return result;
}

Napi::Value Ingestion::IngestParquetAsync(const Napi::CallbackInfo& info) {
  return IngestParquet(info);
}

Napi::Value Ingestion::BatchIngest(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (!db_ || !db_->is_ready()) {
    Napi::Error::New(env, "Database not ready").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  if (info.Length() < 1 || !info[0].IsArray()) {
    Napi::TypeError::New(env, "Expected array of documents").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  Napi::Array docs = info[0].As<Napi::Array>();
  
  is_running_ = true;
  total_items_ = docs.Length();
  processed_items_ = 0;
  
  std::vector<uint64_t> ids;
  
  for (uint32_t i = 0; i < docs.Length(); i++) {
    Napi::Object doc = docs.Get(i).As<Napi::Object>();
    
    if (doc.Has("text")) {
      std::string text = doc.Get("text").As<Napi::String>().Utf8Value();
      vdb::Metadata meta = JSToMetadata(doc.Has("metadata") ? 
        doc.Get("metadata").As<Napi::Object>() : Napi::Object::New(env));
      
      auto result = db_->add_text(text, meta);
      if (result.has_value()) {
        ids.push_back(result.value());
      }
    }
    
    processed_items_++;
  }
  
  is_running_ = false;
  
  Napi::Array ids_arr = Napi::Array::New(env, ids.size());
  for (size_t i = 0; i < ids.size(); i++) {
    ids_arr.Set(static_cast<uint32_t>(i), Napi::Number::New(env, ids[i]));
  }
  
  Napi::Object result = Napi::Object::New(env);
  result.Set("success", true);
  result.Set("ids", ids_arr);
  result.Set("count", Napi::Number::New(env, ids.size()));
  
  return result;
}

Napi::Value Ingestion::GetProgress(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  Napi::Object progress = Napi::Object::New(env);
  progress.Set("total", Napi::Number::New(env, total_items_));
  progress.Set("processed", Napi::Number::New(env, processed_items_));
  progress.Set("running", Napi::Boolean::New(env, is_running_));
  
  if (total_items_ > 0) {
    double percentage = (static_cast<double>(processed_items_) / total_items_) * 100.0;
    progress.Set("percentage", Napi::Number::New(env, percentage));
  } else {
    progress.Set("percentage", Napi::Number::New(env, 0));
  }
  
  return progress;
}

} // namespace hektor_native
