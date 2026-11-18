#include "../src/include/gcache/consistent_hash.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;

using gcache::ConsistentHashMap;
using gcache::HashConfig;

HashConfig MakeStatsConfig()
{
    HashConfig config;

    config.replicas = 1;
    config.min_replicas = 1;
    config.max_replicas = 1;
    config.load_balance_threshold = 1.0;

    config.hash_func = [](const std::string &value)
        -> std::uint32_t
    {
        if (value == "A-0")
            return 10;
        if (value == "B-0")
            return 20;
        if (value == "key-a")
            return 5;
        if (value == "key-b")
            return 15;

        throw std::runtime_error(
            "unexpected test hash input: " + value);
    };

    return config;
}

HashConfig MakeBalanceConfig()
{
    HashConfig config;

    config.replicas = 10;
    config.min_replicas = 1;
    config.max_replicas = 20;
    config.load_balance_threshold = 0.25;

    config.hash_func = [](const std::string &value)
        -> std::uint32_t
    {
        if (value == "hot-key")
        {
            return 0;
        }

        if (value.rfind("A-", 0) == 0)
        {
            const auto index =
                static_cast<std::uint32_t>(
                    std::stoul(value.substr(2)));

            return 100U + index * 100U;
        }

        if (value.rfind("B-", 0) == 0)
        {
            const auto index =
                static_cast<std::uint32_t>(
                    std::stoul(value.substr(2)));

            return 10000U + index * 100U;
        }

        throw std::runtime_error(
            "unexpected test hash input: " + value);
    };

    return config;
}

void TestGetStats()
{
    ConsistentHashMap ring(MakeStatsConfig());

    assert(ring.Add({"A", "B"}));

    assert(ring.Get("key-a") == "A");
    assert(ring.Get("key-a") == "A");
    assert(ring.Get("key-a") == "A");
    assert(ring.Get("key-b") == "B");

    const auto stats = ring.GetStats();

    assert(std::abs(stats.at("A") - 75.0) < 0.001);
    assert(std::abs(stats.at("B") - 25.0) < 0.001);
}

void TestStatsAreZeroWhenNoRequests()
{
    ConsistentHashMap ring(MakeStatsConfig());

    assert(ring.Add({"A", "B"}));

    const auto stats = ring.GetStats();

    assert(stats.at("A") == 0.0);
    assert(stats.at("B") == 0.0);
}

bool WaitForReplicas(
    const ConsistentHashMap &ring,
    int expected_a,
    int expected_b,
    std::chrono::milliseconds timeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (ring.ReplicaCount("A") == expected_a &&
            ring.ReplicaCount("B") == expected_b)
        {
            return true;
        }

        std::this_thread::sleep_for(10ms);
    }

    return ring.ReplicaCount("A") == expected_a &&
           ring.ReplicaCount("B") == expected_b;
}

void TestAdaptiveRebalance()
{
    ConsistentHashMap ring(MakeBalanceConfig());

    assert(ring.Add({"A", "B"}));

    assert(ring.ReplicaCount("A") == 10);
    assert(ring.ReplicaCount("B") == 10);

    // 前 999 次请求全部路由到 A。
    for (int i = 0; i < 999; ++i)
    {
        assert(ring.Get("hot-key") == "A");
    }

    // 等待后台线程至少执行一次检查。
    std::this_thread::sleep_for(1200ms);

    // 样本数不足 1000，不能调整。
    assert(ring.ReplicaCount("A") == 10);
    assert(ring.ReplicaCount("B") == 10);

    // 第 1000 次请求达到样本门槛。
    assert(ring.Get("hot-key") == "A");

    // 等待后台线程下一轮检查并完成调整。
    assert(WaitForReplicas(ring, 5, 20, 2500ms));

    assert(ring.ReplicaCount("A") == 5);
    assert(ring.ReplicaCount("B") == 20);
    assert(ring.VirtualNodeCount() == 25);

    // 再平衡后统计窗口重置。
    const auto stats = ring.GetStats();

    assert(stats.at("A") == 0.0);
    assert(stats.at("B") == 0.0);
}

void TestRemoveClearsStatsEntry()
{
    ConsistentHashMap ring(MakeStatsConfig());

    assert(ring.Add({"A", "B"}));
    assert(ring.Get("key-a") == "A");

    assert(ring.Remove("A"));

    const auto stats = ring.GetStats();

    assert(stats.find("A") == stats.end());
    assert(stats.find("B") != stats.end());
}

void TestReplicaBounds()
{
    HashConfig config = MakeBalanceConfig();

    config.replicas = 10;
    config.min_replicas = 5;
    config.max_replicas = 15;

    ConsistentHashMap ring(config);

    assert(ring.Add({"A", "B"}));

    assert(ring.ReplicaCount("A") == 10);
    assert(ring.ReplicaCount("B") == 10);
}

int main()
{
    TestGetStats();
    TestStatsAreZeroWhenNoRequests();
    TestAdaptiveRebalance();
    TestRemoveClearsStatsEntry();
    TestReplicaBounds();

    std::cout << "All adaptive consistent hash tests passed.\n";
    return 0;
}