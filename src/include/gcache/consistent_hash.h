#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gcache
{
    using HashFunction = std::function<std::uint32_t(const std::string &)>;
    std::uint32_t Crc32IEEE(const std::string &data);

    struct HashConfig
    {
        int replicas = 10;
        int min_replicas = 10;
        int max_replicas = 200;
        HashFunction hash_func = Crc32IEEE;
        double load_balance_threshold = 0.25;
    };

    class ConsistentHashMap
    {
    public:
        explicit ConsistentHashMap(HashConfig config);
        ConsistentHashMap(const ConsistentHashMap &) = delete;
        ConsistentHashMap &operator=(const ConsistentHashMap &) = delete;

        bool Add(const std::vector<std::string> &nodes);
        bool Remove(const std::string &node);
        std::string Get(const std::string &key);

        std::size_t NodeCount() const;
        std::size_t VirtualNodeCount() const;

    private:
        bool AddNodeLocked(const std::string &node);
        void RemoveNodeLocked(const std::string &node);

        mutable std::shared_mutex mtx_;
        HashConfig config_;
        std::vector<std::uint32_t> keys_;
        std::unordered_map<std::uint32_t, std::string> hash_map_;
        std::unordered_map<std::string, int> node_replicas_;
    };
};