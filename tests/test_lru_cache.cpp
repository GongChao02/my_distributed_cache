#include "../src/include/gcache/cache.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using gcache::ByteView;

using gcache::LRUCache;

void Check(bool ok, const std::string &message)
{
    if (!ok)
        throw std::runtime_error(message);
}

void TestAccessOrderAndBytes()
{
    std::vector<std::string> evicted;
    LRUCache cache(11, [&](std::string key, ByteView)
                   { evicted.push_back(key); });
    cache.Set("a", ByteView("111")); // 4
    cache.Set("b", ByteView("22"));  // 3
    cache.Set("c", ByteView("333")); // 4, total 11
    Check(cache.Get("a").has_value(), "a should exist");
    cache.Set("d", ByteView("4")); // +2, remove b(-3)
    Check(!cache.Get("b").has_value(), "b must be oldest");
    cache.Set("a", ByteView("12345")); // +2, remove c(-4)
    Check(!cache.Get("c").has_value(), "c must be evicted");
    Check(cache.Get("a")->ToString() == "12345", "a update value");
    Check(evicted == std::vector<std::string>({"b", "c"}), "eviction order");
    cache.Delete("d");
    Check(!cache.Get("d").has_value(), "delete d");
    Check(evicted == std::vector<std::string>({"b", "c", "d"}), "delete callback");
    cache.Delete("absent");
    Check(evicted.size() == 3, "missing delete callback should not fire");
}

void TestBinaryAndEmpty()
{
    LRUCache cache(60);
    std::string raw("A\0B", 3);
    cache.Set("raw", ByteView(raw));
    auto copied = cache.Get("raw");
    Check(copied.has_value(), "binary hit");
    Check(copied->Len() == 3, "binary length");
    Check(copied->ToString() == raw, "binary bytes");
    cache.Set("empty", ByteView(std::string("")));
    auto empty = cache.Get("empty");
    Check(empty.has_value() && empty->Len() == 0, "empty is hit, not miss");
}

void TestOversizedAndNoLimit()
{
    std::vector<std::string> removed;
    LRUCache limited(3, [&](std::string key, ByteView)
                     { removed.push_back(key); });
    limited.Set("x", ByteView("123"));
    Check(!limited.Get("x").has_value(), "oversize removed immediately");
    Check(removed == std::vector<std::string>({"x"}), "oversize callback");

    LRUCache unlimited(0);
    for (int i = 0; i < 30; ++i)
    {
        unlimited.Set("key" + std::to_string(i), ByteView(std::string(30, 'z')));
    }
    Check(unlimited.Get("key0").has_value(), "0 should mean unlimited");
}

void TestRemoveOldestAndCopy()
{
    std::vector<std::string> evicted;
    LRUCache cache(20, [&](std::string key, ByteView)
                   { evicted.push_back(key); });
    cache.RemoveOldest();
    Check(evicted.empty(), "empty oldest no-op");
    cache.Set("a", ByteView("first"));
    cache.Set("b", ByteView("second"));
    auto copy = cache.Get("a");
    copy->data_[0] = 'X';
    Check(cache.Get("a")->ToString() == "first", "Get returns independent copy");
    cache.RemoveOldest();
    Check(!cache.Get("b").has_value(), "manual oldest removes b");
    Check(evicted == std::vector<std::string>({"b"}), "manual oldest callback");
}

void TestSimpleThreads()
{
    LRUCache cache(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
    {
        threads.emplace_back([i, &cache]
                             {
      const auto key = "key-" + std::to_string(i);
      for (int j = 0; j < 200; ++j) {
        cache.Set(key, ByteView(std::to_string(j)));
        (void)cache.Get(key);
      } });
    }
    for (auto &worker : threads)
        worker.join();
    for (int i = 0; i < 4; ++i)
    {
        Check(cache.Get("key-" + std::to_string(i)).has_value(), "thread key exists");
    }
}

void TestInvalidCapacity()
{
    bool thrown = false;
    try
    {
        LRUCache bad(-1);
        (void)bad;
    }
    catch (const std::invalid_argument &)
    {
        thrown = true;
    }
    Check(thrown, "reject negative max_bytes");
}

int main()
{
    try
    {
        TestAccessOrderAndBytes();
        TestBinaryAndEmpty();
        TestOversizedAndNoLimit();
        TestRemoveOldestAndCopy();
        TestSimpleThreads();
        TestInvalidCapacity();
        std::cout << "PASS: LRUCache tests\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}