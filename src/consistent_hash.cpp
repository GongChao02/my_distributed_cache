#include "include/gcache/consistent_hash.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <math.h>

namespace gcache
{
    std::uint32_t Crc32IEEE(const std::string &data)
    {
        std::uint32_t crc = 0xFFFFFFFFU;
        for (unsigned char byte : data)
        {
            crc ^= static_cast<std::uint32_t>(byte);
            for (int bit = 0; bit < 8; ++bit)
            {
                if ((crc & 1U) != 0U)
                    crc = (crc >> 1U) ^ 0xEDB88320U;
                else
                    crc >>= 1U;
            }
        }
        return crc ^ 0xFFFFFFFFU;
    }

    std::size_t ConsistentHashMap::NodeCount() const
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return node_replicas_.size();
    }

    std::size_t ConsistentHashMap::VirtualNodeCount() const
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        return keys_.size();
    }

    ConsistentHashMap::ConsistentHashMap(HashConfig config) : config_(std::move(config))
    {
        if (config_.replicas <= 0)
            throw std::invalid_argument("replicas must be positive");

        if (config_.min_replicas <= 0 ||
            config_.max_replicas < config_.min_replicas ||
            config_.replicas < config_.min_replicas ||
            config_.replicas > config_.max_replicas)
            throw std::invalid_argument("replica bounds are inconsistent");

        if (!config_.hash_func)
            throw std::invalid_argument("hash_func must be callable");

        if (config_.load_balance_threshold <= 0.0 ||
            config_.load_balance_threshold > 1.0)
            throw std::invalid_argument("load_balance_threshold must be in (0, 1]");

        StartBalancer();
    }

    ConsistentHashMap::~ConsistentHashMap()
    {
        StopBalancer();
    }

    bool ConsistentHashMap::AddNodeLocked(const std::string &node, int replicas)
    {
        if (node.empty() || replicas <= 0)
            return false;

        if (node_replicas_.find(node) != node_replicas_.end())
            return false;

        std::vector<std::uint32_t> new_hashes;
        new_hashes.reserve(static_cast<std::size_t>(replicas));
        for (int i = 0; i < replicas; ++i)
        {
            const std::string virtual_key = node + "-" + std::to_string(i);
            const std::uint32_t hash = config_.hash_func(virtual_key);

            if (hash_map_.find(hash) != hash_map_.end() || std::find(new_hashes.begin(), new_hashes.end(), hash) != new_hashes.end())
                return false;

            new_hashes.push_back(hash);
        }

        for (const std::uint32_t hash : new_hashes)
        {
            keys_.push_back(hash);
            hash_map_.emplace(hash, node);
        }

        node_replicas_.emplace(node, config_.replicas);

        node_counts_.try_emplace(node, 0LL);

        return true;
    }

    void ConsistentHashMap::RemoveNodeLocked(const std::string &node)
    {
        const auto replicas_it = node_replicas_.find(node);
        if (replicas_it == node_replicas_.end())
            return;
        for (int i = 0; i < replicas_it->second; ++i)
        {
            const std::string virtual_key = node + "-" + std::to_string(i);
            const std::uint32_t hash = config_.hash_func(virtual_key);
            const auto map_it = hash_map_.find(hash);

            if (map_it != hash_map_.end() && map_it->second == node)
            {
                hash_map_.erase(map_it);
                const auto new_end = std::remove(keys_.begin(), keys_.end(), hash);
                keys_.erase(new_end, keys_.end());
            }
        }
        node_replicas_.erase(replicas_it);
    }

    bool ConsistentHashMap::Add(const std::vector<std::string> &nodes)
    {
        if (nodes.empty())
            return false;

        std::unique_lock<std::shared_mutex> lock(mtx_);

        std::vector<std::string> added_nodes;
        std::unordered_set<std::string> seen;

        for (const auto &node : nodes)
        {
            if (node.empty() || !seen.insert(node).second || node_replicas_.find(node) != node_replicas_.end())
                continue;
            if (!AddNodeLocked(node, config_.replicas))
            {
                for (const auto &added : added_nodes)
                {
                    RemoveNodeLocked(added);
                    node_counts_.erase(added);
                }
                return false;
            }
            added_nodes.push_back(node);
        }

        if (added_nodes.empty())
            return false;

        std::sort(keys_.begin(), keys_.end());
        return true;
    }

    bool ConsistentHashMap::Remove(const std::string &node)
    {
        if (node.empty())
            return false;
        std::unique_lock<std::shared_mutex> lock(mtx_);

        if (node_replicas_.find(node) == node_replicas_.end())
            return false;

        RemoveNodeLocked(node);
        node_counts_.erase(node);
        return true;
    }

    std::string ConsistentHashMap::Get(const std::string &key)
    {
        if (key.empty())
            return "";

        std::shared_lock<std::shared_mutex> lock(mtx_);

        if (keys_.empty())
            return "";

        const std::uint32_t hash = config_.hash_func(key);

        auto it = std::lower_bound(keys_.begin(), keys_.end(), hash);

        if (it == keys_.end())
            it = keys_.begin();

        const auto node_it = hash_map_.find(*it);

        if (node_it == hash_map_.end())
            return "";

        const std::string &node = node_it->second;

        const auto count_it = node_counts_.find(node);

        if (count_it == node_counts_.end())
            return "";

        ++count_it->second;
        ++total_requests_;

        return node;
    };

    auto ConsistentHashMap::GetStats() const -> std::unordered_map<std::string, double>
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        std::unordered_map<std::string, double> stats;
        const long long total = total_requests_.load();
        for (const auto &[node, replicas] : node_replicas_)
        {
            const auto count_it = node_counts_.find(node);
            const long long count = count_it == node_counts_.end() ? 0 : count_it->second.load();

            if (total == 0)
                stats[node] = 0.0;
            else
                stats[node] = static_cast<double>(count) / static_cast<double>(total) * 100.0;
        }
        return stats;
    }

    void ConsistentHashMap::CheckAndRebalance()
    {
        if (total_requests_.load() < 1000)
            return;

        double max_diff = 0.0;

        {
            std::shared_lock<std::shared_mutex> lock(mtx_);
            if (node_replicas_.empty())
                return;
            const long long current_total_requests = total_requests_.load();
            const double avg_load = static_cast<double>(current_total_requests) / static_cast<double>(node_replicas_.size());
            for (const auto &[node, replicas] : node_replicas_)
            {
                const auto count_it = node_counts_.find(node);
                const long long count = count_it == node_counts_.end() ? 0 : count_it->second.load();
                const double diff = std::abs(static_cast<double>(count) - avg_load);

                if (avg_load > 0.0)
                    max_diff = std::max(max_diff, diff / avg_load);
                else if (diff > 0.0)
                    max_diff = 1.0;
            }
        }

        if (max_diff > config_.load_balance_threshold)
            RebalanceNodes();
    }

    bool ConsistentHashMap::ReplaceNodeReplicasLocked(const std::string &node, int new_replicas)
    {
        if (new_replicas <= 0)
            return false;

        const auto node_it = node_replicas_.find(node);

        if (node_it == node_replicas_.end())
            return false;

        const int old_replicas = node_it->second;

        std::vector<std::uint32_t> new_hashes;
        new_hashes.reserve(static_cast<std::size_t>(new_replicas));

        // 先计算所有新位置并检查碰撞
        for (int i = 0; i < new_replicas; ++i)
        {
            const std::string virtual_key = node + "-" + std::to_string(i);

            const std::uint32_t hash = config_.hash_func(virtual_key);

            const auto existing = hash_map_.find(hash);

            // 只允许新位置和旧位置重合
            if (existing != hash_map_.end() && existing->second != node)
                return false;

            // 同一个节点的新虚拟节点间也不能碰撞
            if (std::find(new_hashes.begin(), new_hashes.end(), hash) != new_hashes.end())
                return false;

            new_hashes.push_back(hash);
        }

        // 然后删除本节点原来的所有虚拟节点
        for (int i = 0; i < old_replicas; ++i)
        {
            const std::string virtual_key = node + "-" + std::to_string(i);

            const std::uint32_t hash = config_.hash_func(virtual_key);

            const auto map_it = hash_map_.find(hash);

            if (map_it != hash_map_.end() && map_it->second == node)
                hash_map_.erase(map_it);

            const auto new_end = std::remove(keys_.begin(), keys_.end(), hash);

            keys_.erase(new_end, keys_.end());
        }

        // 添加新的虚拟节点
        for (const std::uint32_t hash : new_hashes)
        {
            keys_.push_back(hash);
            hash_map_.emplace(hash, node);
        }

        node_it->second = new_replicas;

        return true;
    }

    void ConsistentHashMap::RebalanceNodes()
    {
        std::unique_lock<std::shared_mutex> lock(mtx_);

        if (node_replicas_.empty())
            return;

        const long long current_total_requests =
            total_requests_.load();

        if (current_total_requests < 1000)
            return;

        const double avg_load = static_cast<double>(current_total_requests) / static_cast<double>(node_replicas_.size());

        const std::unordered_map<std::string, int> curr_replicas = node_replicas_;

        std::unordered_map<std::string, long long> curr_counts;

        for (const auto &[node, count] : node_counts_)
            curr_counts[node] = count.load();

        // 对每个真实节点计算新的虚拟节点数量。
        for (const auto &[node, count] : curr_counts)
        {
            const auto replica_it = curr_replicas.find(node);

            if (replica_it == curr_replicas.end())
                continue;

            const int old_replicas = replica_it->second;

            double load_ratio = 1.0;

            if (avg_load > 0.0)
                load_ratio = static_cast<double>(count) / avg_load;
            else if (count > 0)
                load_ratio = 2.0;

            int new_replicas = old_replicas;

            if (load_ratio > 1.0)
                new_replicas = static_cast<int>(std::round(static_cast<double>(old_replicas) / load_ratio));
            else
                new_replicas = static_cast<int>(std::round(static_cast<double>(old_replicas) * (2.0 - load_ratio)));

            new_replicas = std::clamp(new_replicas, config_.min_replicas, config_.max_replicas);

            if (new_replicas == old_replicas)
                continue;

            ReplaceNodeReplicasLocked(node, new_replicas);
        }

        std::sort(keys_.begin(), keys_.end());

        for (auto &[node, count] : node_counts_)
            count.store(0);

        total_requests_.store(0);
    };

    void ConsistentHashMap::StartBalancer()
    {
        is_balancer_stop_.store(false);

        balancer_thread_ = std::thread([this]
                                       {
            while (!is_balancer_stop_.load()) {
                std::unique_lock<std::mutex> wait_lock(balancer_wait_mtx_);

                const bool stopped = balancer_cv_.wait_for(
                    wait_lock,
                    std::chrono::seconds(1),
                    [this] {
                        return is_balancer_stop_.load();
                    }
                );

                if (stopped)
                    return;

                wait_lock.unlock();

                CheckAndRebalance();
            } });
    }

    void ConsistentHashMap::StopBalancer()
    {
        is_balancer_stop_.store(true);

        balancer_cv_.notify_all();

        if (balancer_thread_.joinable())
            balancer_thread_.join();
    }

    int ConsistentHashMap::ReplicaCount(const std::string &node) const
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        const auto it = node_replicas_.find(node);
        return it == node_replicas_.end() ? 0 : it->second;
    }
}