#include "include/gcache/consistent_hash.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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
    }

    bool ConsistentHashMap::AddNodeLocked(const std::string &node)
    {
        std::vector<std::uint32_t> new_hashes;
        new_hashes.reserve(static_cast<std::size_t>(config_.replicas));
        for (int i = 0; i < config_.replicas; ++i)
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
            if (!AddNodeLocked(node))
            {
                for (const auto &added : added_nodes)
                    RemoveNodeLocked(added);
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

        return node_it->second;
    };
}