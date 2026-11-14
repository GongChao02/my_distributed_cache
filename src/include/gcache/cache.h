#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>    // std::function：保存回调
#include <list>          // std::list：访问顺序
#include <mutex>         // std::mutex：保护内部共享状态
#include <optional>      // std::optional：未命中、可选淘汰结果
#include <unordered_map> // key -> 链表迭代器

namespace gcache
{

    struct ByteView
    {
        std::vector<char> data_{};

        explicit ByteView(const std::string &str);

        std::int64_t Len() const;
        std::string ToString() const;
    };

    struct Entry
    {
        std::string key_;
        ByteView value_;

        Entry(std::string key, const ByteView &value);

        bool operator==(const Entry &other) const;
    };

    using ByteViewOptional = std::optional<ByteView>;

    class LRUCache
    {
    public:
        using EvictedFunc = std::function<void(std::string, ByteView)>;

        explicit LRUCache(std::int64_t max_bytes, EvictedFunc on_evicted = {});

        ByteViewOptional Get(const std::string &key);
        void Set(const std::string &key, const ByteView &value);
        void Delete(const std::string &key);
        void RemoveOldest();

    private:
        static std::uint64_t SizeOf(const Entry &entry);
        std::optional<Entry> RemoveOldestLocked();

        std::int64_t bytes_{0};
        std::int64_t max_bytes_;
        EvictedFunc evicted_func_;
        std::list<Entry> list_;
        std::unordered_map<std::string, std::list<Entry>::iterator> cache_;
        std::mutex mtx_;
    };

} // namespace gcache