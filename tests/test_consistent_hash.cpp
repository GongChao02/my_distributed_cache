#include "../src/include/gcache/consistent_hash.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using gcache::ConsistentHashMap;
using gcache::HashConfig;

HashConfig MakeControlledConfig()
{
    HashConfig config;

    config.replicas = 1;
    config.min_replicas = 1;
    config.max_replicas = 1;

    const std::unordered_map<std::string, std::uint32_t> positions{
        {"A-0", 10},
        {"B-0", 25},
        {"C-0", 35},

        {"key-5", 5},
        {"key-20", 20},
        {"key-30", 30},
        {"key-38", 38},
        {"key-50", 50},
        {"key-60", 60},
    };

    config.hash_func = [positions](const std::string &value)
    {
        const auto it = positions.find(value);

        if (it == positions.end())
        {
            throw std::runtime_error(
                "missing test hash mapping: " + value);
        }

        return it->second;
    };

    return config;
}

void TestEmptyRingAndInvalidInput()
{
    ConsistentHashMap ring(MakeControlledConfig());

    assert(ring.Get("key-5").empty());
    assert(ring.Get("").empty());
    assert(!ring.Add({}));
    assert(!ring.Remove("A"));
}

void TestClockwiseLookupAndWrapAround()
{
    ConsistentHashMap ring(MakeControlledConfig());

    assert(ring.Add({"A", "B"}));

    assert(ring.NodeCount() == 2);
    assert(ring.VirtualNodeCount() == 2);

    assert(ring.Get("key-5") == "A");
    assert(ring.Get("key-20") == "B");
    assert(ring.Get("key-30") == "A");
    assert(ring.Get("key-38") == "A");
    assert(ring.Get("key-50") == "A");
    assert(ring.Get("key-60") == "A");
}

void TestAddNodeOnlyMovesItsInterval()
{
    ConsistentHashMap ring(MakeControlledConfig());

    assert(ring.Add({"A", "B"}));

    assert(ring.Get("key-30") == "A");
    assert(ring.Get("key-38") == "A");
    assert(ring.Get("key-50") == "A");

    assert(ring.Add({"C"}));

    assert(ring.Get("key-30") == "C");
    assert(ring.Get("key-38") == "A");
    assert(ring.Get("key-50") == "A");

    assert(ring.Remove("C"));

    assert(ring.Get("key-30") == "A");
    assert(ring.Get("key-38") == "A");
}

void TestDuplicateNodeIsIgnored()
{
    ConsistentHashMap ring(MakeControlledConfig());

    assert(ring.Add({"A"}));
    assert(!ring.Add({"A", "A"}));

    assert(ring.NodeCount() == 1);
    assert(ring.VirtualNodeCount() == 1);
}

void TestCollisionRejectsWithoutCorruptingRing()
{
    HashConfig config;

    config.replicas = 1;
    config.min_replicas = 1;
    config.max_replicas = 1;

    config.hash_func = [](const std::string &)
    {
        return 42U;
    };

    ConsistentHashMap ring(config);

    assert(ring.Add({"A"}));
    assert(!ring.Add({"B"}));

    assert(ring.NodeCount() == 1);
    assert(ring.VirtualNodeCount() == 1);
    assert(ring.Get("some-key") == "A");
}

void TestBatchAddRollsBackOnCollision()
{
    HashConfig config;

    config.replicas = 1;
    config.min_replicas = 1;
    config.max_replicas = 1;

    config.hash_func = [](const std::string &value)
        -> std::uint32_t
    {
        if (value == "A-0")
        {
            return 10;
        }
        if (value == "B-0")
        {
            return 20;
        }
        if (value == "C-0")
        {
            return 10;
        }
        if (value == "key")
        {
            return 15;
        }
        return 99;
    };

    ConsistentHashMap ring(config);

    assert(ring.Add({"A"}));
    assert(!ring.Add({"B", "C"}));

    assert(ring.NodeCount() == 1);
    assert(ring.VirtualNodeCount() == 1);
    assert(ring.Get("key") == "A");
}

void TestConcurrentReadersAndMembershipChanges()
{
    ConsistentHashMap ring(MakeControlledConfig());
    assert(ring.Add({"A", "B"}));

    std::atomic<bool> failed{false};
    std::vector<std::thread> readers;

    for (int i = 0; i < 4; ++i)
    {
        readers.emplace_back([&]
                             {
      for (int j = 0; j < 2000; ++j) {
        const std::string node = ring.Get("key-30");
        if (node != "A" && node != "C") {
          failed.store(true);
        }
      } });
    }

    std::thread writer([&]
                       {
    for (int i = 0; i < 200; ++i) {
      if (ring.Add({"C"})) {
        ring.Remove("C");
      }
    } });

    for (auto &reader : readers)
    {
        reader.join();
    }
    writer.join();

    assert(!failed.load());
    assert(ring.NodeCount() == 2);
    assert(ring.VirtualNodeCount() == 2);
}

void TestCrc32KnownVector()
{
    assert(gcache::Crc32IEEE("123456789") == 0xCBF43926U);
}

int main()
{
    TestEmptyRingAndInvalidInput();
    TestClockwiseLookupAndWrapAround();
    TestAddNodeOnlyMovesItsInterval();
    TestDuplicateNodeIsIgnored();
    TestCollisionRejectsWithoutCorruptingRing();
    TestBatchAddRollsBackOnCollision();
    TestConcurrentReadersAndMembershipChanges();
    TestCrc32KnownVector();

    std::cout << "All consistent hash tests passed.\n";
    return 0;
}