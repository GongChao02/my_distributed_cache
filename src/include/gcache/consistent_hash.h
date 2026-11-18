#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>

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
        ~ConsistentHashMap();
        ConsistentHashMap(const ConsistentHashMap &) = delete;
        ConsistentHashMap &operator=(const ConsistentHashMap &) = delete;

        bool Add(const std::vector<std::string> &nodes);
        bool Remove(const std::string &node);
        std::string Get(const std::string &key);

        auto GetStats() const -> std::unordered_map<std::string, double>;

        std::size_t NodeCount() const;
        std::size_t VirtualNodeCount() const;
        int ReplicaCount(const std::string &key) const;

    private:
        bool AddNodeLocked(const std::string &node, int replicas);
        void RemoveNodeLocked(const std::string &node);

        bool ReplaceNodeReplicasLocked(const std::string &node, int new_replicas);

        void CheckAndRebalance();
        void RebalanceNodes();

        void StartBalancer();
        void StopBalancer();

        mutable std::shared_mutex mtx_;
        HashConfig config_;
        std::vector<std::uint32_t> keys_;
        std::unordered_map<std::uint32_t, std::string> hash_map_;
        std::unordered_map<std::string, int> node_replicas_;

        std::unordered_map<std::string, std::atomic<long long>> node_counts_;

        std::atomic<long long> total_requests_{0};

        std::thread balancer_thread_;
        std::atomic<bool> is_balancer_stop_{false};

        std::mutex balancer_wait_mtx_;
        std::condition_variable balancer_cv_;
    };
};