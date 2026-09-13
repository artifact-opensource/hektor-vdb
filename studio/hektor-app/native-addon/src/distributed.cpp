#include "distributed.h"

#include "database.h"
#include "vdb/replication.hpp"

#include <string>
#include <vector>

namespace hektor_native {
namespace {

vdb::NodeConfig ParseNode(const Napi::Object& obj) {
    vdb::NodeConfig node;
    if (obj.Has("nodeId")) node.node_id = obj.Get("nodeId").ToString().Utf8Value();
    if (obj.Has("host")) node.host = obj.Get("host").ToString().Utf8Value();
    if (obj.Has("port")) node.port = static_cast<uint16_t>(obj.Get("port").As<Napi::Number>().Uint32Value());
    if (obj.Has("isPrimary")) node.is_primary = obj.Get("isPrimary").As<Napi::Boolean>().Value();
    if (obj.Has("priority")) node.priority = obj.Get("priority").As<Napi::Number>().Int32Value();
    return node;
}

Napi::Object NodeToJs(Napi::Env env, const vdb::NodeConfig& node) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("nodeId", Napi::String::New(env, node.node_id));
    obj.Set("host", Napi::String::New(env, node.host));
    obj.Set("port", Napi::Number::New(env, node.port));
    obj.Set("isPrimary", Napi::Boolean::New(env, node.is_primary));
    obj.Set("priority", Napi::Number::New(env, node.priority));
    return obj;
}

vdb::ReplicationConfig ParseReplicationConfig(const Napi::Value& value) {
    vdb::ReplicationConfig config;
    if (!value.IsObject()) return config;
    auto obj = value.As<Napi::Object>();
    if (obj.Has("mode")) {
        const auto mode = obj.Get("mode").ToString().Utf8Value();
        if (mode == "async") config.mode = vdb::ReplicationMode::Async;
        else if (mode == "sync") config.mode = vdb::ReplicationMode::Sync;
        else if (mode == "semi-sync") config.mode = vdb::ReplicationMode::SemiSync;
    }
    if (obj.Has("nodes") && obj.Get("nodes").IsArray()) {
        auto nodes = obj.Get("nodes").As<Napi::Array>();
        for (uint32_t i = 0; i < nodes.Length(); ++i) config.nodes.push_back(ParseNode(nodes.Get(i).As<Napi::Object>()));
    }
    if (obj.Has("minReplicas")) config.min_replicas = obj.Get("minReplicas").As<Napi::Number>().Int64Value();
    if (obj.Has("syncTimeoutMs")) config.sync_timeout_ms = obj.Get("syncTimeoutMs").As<Napi::Number>().Uint32Value();
    if (obj.Has("heartbeatIntervalMs")) config.heartbeat_interval_ms = obj.Get("heartbeatIntervalMs").As<Napi::Number>().Uint32Value();
    if (obj.Has("electionTimeoutMs")) config.election_timeout_ms = obj.Get("electionTimeoutMs").As<Napi::Number>().Uint32Value();
    return config;
}

vdb::ShardingConfig ParseShardingConfig(const Napi::Value& value) {
    vdb::ShardingConfig config;
    if (!value.IsObject()) return config;
    auto obj = value.As<Napi::Object>();
    if (obj.Has("strategy")) {
        const auto strategy = obj.Get("strategy").ToString().Utf8Value();
        if (strategy == "hash") config.strategy = vdb::ShardingStrategy::Hash;
        else if (strategy == "range") config.strategy = vdb::ShardingStrategy::Range;
        else if (strategy == "consistent") config.strategy = vdb::ShardingStrategy::Consistent;
    }
    if (obj.Has("numShards")) config.num_shards = obj.Get("numShards").As<Napi::Number>().Int64Value();
    if (obj.Has("enableAutoResharding")) config.enable_auto_resharding = obj.Get("enableAutoResharding").As<Napi::Boolean>().Value();
    if (obj.Has("reshardThresholdItems")) config.reshard_threshold_items = obj.Get("reshardThresholdItems").As<Napi::Number>().Int64Value();
    if (obj.Has("reshardThresholdImbalance")) config.reshard_threshold_imbalance = obj.Get("reshardThresholdImbalance").As<Napi::Number>().FloatValue();
    return config;
}

Napi::Array NodesToJs(Napi::Env env, const std::vector<vdb::NodeConfig>& nodes) {
    Napi::Array out = Napi::Array::New(env, nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) out.Set(static_cast<uint32_t>(i), NodeToJs(env, nodes[i]));
    return out;
}

} // namespace

Napi::Object ReplicationManagerWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "ReplicationManager", {
        InstanceMethod("start", &ReplicationManagerWrap::Start),
        InstanceMethod("stop", &ReplicationManagerWrap::Stop),
        InstanceMethod("replicateAdd", &ReplicationManagerWrap::ReplicateAdd),
        InstanceMethod("replicateRemove", &ReplicationManagerWrap::ReplicateRemove),
        InstanceMethod("replicateUpdate", &ReplicationManagerWrap::ReplicateUpdate),
        InstanceMethod("addReplica", &ReplicationManagerWrap::AddReplica),
        InstanceMethod("removeReplica", &ReplicationManagerWrap::RemoveReplica),
        InstanceMethod("getReplicas", &ReplicationManagerWrap::GetReplicas),
        InstanceMethod("isHealthy", &ReplicationManagerWrap::IsHealthy),
        InstanceMethod("getPrimaryNode", &ReplicationManagerWrap::GetPrimaryNode),
        InstanceMethod("promoteToPrimary", &ReplicationManagerWrap::PromoteToPrimary),
        InstanceMethod("demoteFromPrimary", &ReplicationManagerWrap::DemoteFromPrimary),
        InstanceMethod("triggerFailover", &ReplicationManagerWrap::TriggerFailover),
        InstanceMethod("setFailoverCallback", &ReplicationManagerWrap::SetFailoverCallback),
    });
    exports.Set("ReplicationManager", func);
    return exports;
}

ReplicationManagerWrap::ReplicationManagerWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ReplicationManagerWrap>(info) { manager_ = std::make_unique<vdb::ReplicationManager>(ParseReplicationConfig(info.Length() > 0 ? info[0] : info.Env().Undefined())); }
Napi::Value ReplicationManagerWrap::Start(const Napi::CallbackInfo& info) { UnwrapResult(manager_->start(), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::Stop(const Napi::CallbackInfo& info) { UnwrapResult(manager_->stop(), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::ReplicateAdd(const Napi::CallbackInfo& info) { UnwrapResult(manager_->replicate_add(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()), JSToVector(info[1].As<Napi::Array>()), info.Length() > 2 && info[2].IsObject() ? JSToMetadata(info[2].As<Napi::Object>()) : vdb::Metadata{}), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::ReplicateRemove(const Napi::CallbackInfo& info) { UnwrapResult(manager_->replicate_remove(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::ReplicateUpdate(const Napi::CallbackInfo& info) { UnwrapResult(manager_->replicate_update(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()), JSToMetadata(info[1].As<Napi::Object>())), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::AddReplica(const Napi::CallbackInfo& info) { UnwrapResult(manager_->add_replica(ParseNode(info[0].As<Napi::Object>())), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::RemoveReplica(const Napi::CallbackInfo& info) { UnwrapResult(manager_->remove_replica(info[0].ToString().Utf8Value()), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ReplicationManagerWrap::GetReplicas(const Napi::CallbackInfo& info) { auto replicas = UnwrapResult(manager_->get_replicas(), info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); return NodesToJs(info.Env(), replicas); }
Napi::Value ReplicationManagerWrap::IsHealthy(const Napi::CallbackInfo& info) { auto healthy = UnwrapResult(manager_->is_healthy(), info.Env()); return Napi::Boolean::New(info.Env(), healthy); }
Napi::Value ReplicationManagerWrap::GetPrimaryNode(const Napi::CallbackInfo& info) { auto node = UnwrapResult(manager_->get_primary_node(), info.Env()); return Napi::String::New(info.Env(), node); }
Napi::Value ReplicationManagerWrap::PromoteToPrimary(const Napi::CallbackInfo& info) { UnwrapResult(manager_->promote_to_primary(), info.Env()); return info.Env().Undefined(); }
Napi::Value ReplicationManagerWrap::DemoteFromPrimary(const Napi::CallbackInfo& info) { UnwrapResult(manager_->demote_from_primary(), info.Env()); return info.Env().Undefined(); }
Napi::Value ReplicationManagerWrap::TriggerFailover(const Napi::CallbackInfo& info) { UnwrapResult(manager_->trigger_failover(), info.Env()); return info.Env().Undefined(); }
Napi::Value ReplicationManagerWrap::SetFailoverCallback(const Napi::CallbackInfo& info) { failover_callback_ = Napi::Persistent(info[0].As<Napi::Function>()); UnwrapResult(manager_->set_failover_callback([this](const std::string& node_id) { if (!failover_callback_.IsEmpty()) failover_callback_.Call({Napi::String::New(failover_callback_.Env(), node_id)}); }), info.Env()); return info.Env().Undefined(); }

Napi::Object ShardingManagerWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "ShardingManager", {
        InstanceMethod("start", &ShardingManagerWrap::Start),
        InstanceMethod("stop", &ShardingManagerWrap::Stop),
        InstanceMethod("getShardForId", &ShardingManagerWrap::GetShardForId),
        InstanceMethod("getShardForKey", &ShardingManagerWrap::GetShardForKey),
        InstanceMethod("getAllShards", &ShardingManagerWrap::GetAllShards),
        InstanceMethod("addShard", &ShardingManagerWrap::AddShard),
        InstanceMethod("removeShard", &ShardingManagerWrap::RemoveShard),
        InstanceMethod("rebalanceShards", &ShardingManagerWrap::RebalanceShards),
        InstanceMethod("getShardSize", &ShardingManagerWrap::GetShardSize),
        InstanceMethod("getShardImbalance", &ShardingManagerWrap::GetShardImbalance),
        InstanceMethod("needsResharding", &ShardingManagerWrap::NeedsResharding),
        InstanceMethod("triggerResharding", &ShardingManagerWrap::TriggerResharding),
        InstanceMethod("setReshardingCallback", &ShardingManagerWrap::SetReshardingCallback),
    });
    exports.Set("ShardingManager", func);
    return exports;
}

ShardingManagerWrap::ShardingManagerWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<ShardingManagerWrap>(info) { manager_ = std::make_unique<vdb::ShardingManager>(ParseShardingConfig(info.Length() > 0 ? info[0] : info.Env().Undefined())); }
Napi::Value ShardingManagerWrap::Start(const Napi::CallbackInfo& info) { UnwrapResult(manager_->start(), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ShardingManagerWrap::Stop(const Napi::CallbackInfo& info) { UnwrapResult(manager_->stop(), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ShardingManagerWrap::GetShardForId(const Napi::CallbackInfo& info) { auto shard = UnwrapResult(manager_->get_shard_for_id(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())), info.Env()); return Napi::String::New(info.Env(), shard); }
Napi::Value ShardingManagerWrap::GetShardForKey(const Napi::CallbackInfo& info) { auto shard = UnwrapResult(manager_->get_shard_for_key(info[0].ToString().Utf8Value()), info.Env()); return Napi::String::New(info.Env(), shard); }
Napi::Value ShardingManagerWrap::GetAllShards(const Napi::CallbackInfo& info) { auto shards = UnwrapResult(manager_->get_all_shards(), info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), shards.size()); for (size_t i = 0; i < shards.size(); ++i) out.Set(static_cast<uint32_t>(i), Napi::String::New(info.Env(), shards[i])); return out; }
Napi::Value ShardingManagerWrap::AddShard(const Napi::CallbackInfo& info) { vdb::ShardConfig shard; auto obj = info[0].As<Napi::Object>(); shard.shard_id = obj.Get("shardId").ToString().Utf8Value(); if (obj.Has("startRange")) shard.start_range = obj.Get("startRange").As<Napi::Number>().Int64Value(); if (obj.Has("endRange")) shard.end_range = obj.Get("endRange").As<Napi::Number>().Int64Value(); if (obj.Has("replicas") && obj.Get("replicas").IsArray()) { auto arr = obj.Get("replicas").As<Napi::Array>(); for (uint32_t i = 0; i < arr.Length(); ++i) shard.replicas.push_back(ParseNode(arr.Get(i).As<Napi::Object>())); } UnwrapResult(manager_->add_shard(shard), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ShardingManagerWrap::RemoveShard(const Napi::CallbackInfo& info) { UnwrapResult(manager_->remove_shard(info[0].ToString().Utf8Value()), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value ShardingManagerWrap::RebalanceShards(const Napi::CallbackInfo& info) { UnwrapResult(manager_->rebalance_shards(), info.Env()); return info.Env().Undefined(); }
Napi::Value ShardingManagerWrap::GetShardSize(const Napi::CallbackInfo& info) { auto size = UnwrapResult(manager_->get_shard_size(info[0].ToString().Utf8Value()), info.Env()); return Napi::Number::New(info.Env(), static_cast<double>(size)); }
Napi::Value ShardingManagerWrap::GetShardImbalance(const Napi::CallbackInfo& info) { auto imbalance = UnwrapResult(manager_->get_shard_imbalance(), info.Env()); return Napi::Number::New(info.Env(), imbalance); }
Napi::Value ShardingManagerWrap::NeedsResharding(const Napi::CallbackInfo& info) { auto needs = UnwrapResult(manager_->needs_resharding(), info.Env()); return Napi::Boolean::New(info.Env(), needs); }
Napi::Value ShardingManagerWrap::TriggerResharding(const Napi::CallbackInfo& info) { UnwrapResult(manager_->trigger_resharding(), info.Env()); return info.Env().Undefined(); }
Napi::Value ShardingManagerWrap::SetReshardingCallback(const Napi::CallbackInfo& info) { resharding_callback_ = Napi::Persistent(info[0].As<Napi::Function>()); UnwrapResult(manager_->set_resharding_callback([this](size_t from, size_t to) { if (!resharding_callback_.IsEmpty()) resharding_callback_.Call({Napi::Number::New(resharding_callback_.Env(), static_cast<double>(from)), Napi::Number::New(resharding_callback_.Env(), static_cast<double>(to))}); }), info.Env()); return info.Env().Undefined(); }

Napi::Object DistributedVectorDatabaseWrap::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "DistributedVectorDatabase", {
        InstanceMethod("init", &DistributedVectorDatabaseWrap::Init),
        InstanceMethod("close", &DistributedVectorDatabaseWrap::Close),
        InstanceMethod("add", &DistributedVectorDatabaseWrap::Add),
        InstanceMethod("remove", &DistributedVectorDatabaseWrap::Remove),
        InstanceMethod("get", &DistributedVectorDatabaseWrap::Get),
        InstanceMethod("updateMetadata", &DistributedVectorDatabaseWrap::UpdateMetadata),
        InstanceMethod("search", &DistributedVectorDatabaseWrap::Search),
        InstanceMethod("addNode", &DistributedVectorDatabaseWrap::AddNode),
        InstanceMethod("removeNode", &DistributedVectorDatabaseWrap::RemoveNode),
        InstanceMethod("getAllNodes", &DistributedVectorDatabaseWrap::GetAllNodes),
        InstanceMethod("isClusterHealthy", &DistributedVectorDatabaseWrap::IsClusterHealthy),
    });
    exports.Set("DistributedVectorDatabase", func);
    return exports;
}

DistributedVectorDatabaseWrap::DistributedVectorDatabaseWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<DistributedVectorDatabaseWrap>(info) { db_ = std::make_unique<vdb::DistributedVectorDatabase>(ParseReplicationConfig(info.Length() > 0 ? info[0] : info.Env().Undefined()), ParseShardingConfig(info.Length() > 1 ? info[1] : info.Env().Undefined())); }
Napi::Value DistributedVectorDatabaseWrap::Init(const Napi::CallbackInfo& info) { UnwrapResult(db_->init(info[0].As<Napi::Number>().Uint32Value(), info.Length() > 1 ? static_cast<vdb::DistanceMetric>(info[1].As<Napi::Number>().Uint32Value()) : vdb::DistanceMetric::Cosine), info.Env()); return Napi::Boolean::New(info.Env(), !info.Env().IsExceptionPending()); }
Napi::Value DistributedVectorDatabaseWrap::Close(const Napi::CallbackInfo& info) { UnwrapResult(db_->close(), info.Env()); return info.Env().Undefined(); }
Napi::Value DistributedVectorDatabaseWrap::Add(const Napi::CallbackInfo& info) { auto id = UnwrapResult(db_->add(JSToVector(info[0].As<Napi::Array>()), info.Length() > 1 && info[1].IsObject() ? JSToMetadata(info[1].As<Napi::Object>()) : vdb::Metadata{}), info.Env()); return Napi::Number::New(info.Env(), static_cast<double>(id)); }
Napi::Value DistributedVectorDatabaseWrap::Remove(const Napi::CallbackInfo& info) { auto removed = UnwrapResult(db_->remove(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())), info.Env()); return Napi::Boolean::New(info.Env(), removed); }
Napi::Value DistributedVectorDatabaseWrap::Get(const Napi::CallbackInfo& info) { auto vector = UnwrapResult(db_->get(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value())), info.Env()); return vector ? VectorToJS(info.Env(), std::vector<float>(vector->begin(), vector->end())) : info.Env().Null(); }
Napi::Value DistributedVectorDatabaseWrap::UpdateMetadata(const Napi::CallbackInfo& info) { UnwrapResult(db_->update_metadata(static_cast<vdb::VectorId>(info[0].As<Napi::Number>().Int64Value()), JSToMetadata(info[1].As<Napi::Object>())), info.Env()); return info.Env().Undefined(); }
Napi::Value DistributedVectorDatabaseWrap::Search(const Napi::CallbackInfo& info) { auto results = UnwrapResult(db_->search(JSToVector(info[0].As<Napi::Array>()), info[1].As<Napi::Number>().Uint32Value()), info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); Napi::Array out = Napi::Array::New(info.Env(), results.size()); for (size_t i = 0; i < results.size(); ++i) { Napi::Object item = Napi::Object::New(info.Env()); item.Set("id", Napi::Number::New(info.Env(), static_cast<double>(results[i].id))); item.Set("distance", Napi::Number::New(info.Env(), results[i].distance)); item.Set("score", Napi::Number::New(info.Env(), results[i].score)); if (results[i].metadata) item.Set("metadata", MetadataToJS(info.Env(), *results[i].metadata)); out.Set(static_cast<uint32_t>(i), item);} return out; }
Napi::Value DistributedVectorDatabaseWrap::AddNode(const Napi::CallbackInfo& info) { UnwrapResult(db_->add_node(ParseNode(info[0].As<Napi::Object>())), info.Env()); return info.Env().Undefined(); }
Napi::Value DistributedVectorDatabaseWrap::RemoveNode(const Napi::CallbackInfo& info) { UnwrapResult(db_->remove_node(info[0].ToString().Utf8Value()), info.Env()); return info.Env().Undefined(); }
Napi::Value DistributedVectorDatabaseWrap::GetAllNodes(const Napi::CallbackInfo& info) { auto nodes = UnwrapResult(db_->get_all_nodes(), info.Env()); if (info.Env().IsExceptionPending()) return info.Env().Null(); return NodesToJs(info.Env(), nodes); }
Napi::Value DistributedVectorDatabaseWrap::IsClusterHealthy(const Napi::CallbackInfo& info) { auto healthy = UnwrapResult(db_->is_cluster_healthy(), info.Env()); return Napi::Boolean::New(info.Env(), healthy); }

} // namespace hektor_native
