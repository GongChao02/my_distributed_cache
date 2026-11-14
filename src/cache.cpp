#include "./include/gcache/cache.h"
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace gcache
{
    ByteView::ByteView(const std::string &str) : data_(str.begin(), str.end()) {}
    std::int64_t ByteView::Len() const { return static_cast<std::int64_t>(data_.size()); }
    std::string ByteView::ToString() const { return std::string(data_.begin(), data_.end()); }

    Entry::Entry(std::string key, const ByteView &value) : key_(std::move(key)), value_(value) {};
    bool Entry::operator==(const Entry &other) const
    {
        return key_ == other.key_ && value_.ToString() == other.value_.ToString();
    }

    LRUCache::LRUCache(std::int64_t max_bytes, EvictedFunc on_evicted) : max_bytes_(max_bytes), evicted_func_(std::move(on_evicted))
    {
        if (max_bytes < 0)
            throw std::invalid_argument("max_bytes must be >= 0");
    };
    std::uint64_t LRUCache::SizeOf(const Entry &entry)
    {
        return static_cast<std::int64_t>(entry.key_.size()) + entry.value_.Len();
    }

    ByteViewOptional LRUCache::Get(const std::string &key)
    {
        // 1.未命中，返回optnull
        // 2.命中，返回值
        // 3.命中，把对应value放到列表头
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_.find(key);
        if (it == cache_.end())
            return std::nullopt;
        list_.splice(list_.begin(), list_, it->second);
        return it->second->value_;
    }

    std::optional<Entry> LRUCache::RemoveOldestLocked()
    {
        // 1.若为空，返回nullopt
        // 2.不为空，找到尾节
        // 3.保存要删的节点，准备用于返回后的回调函数
        // 4.map删掉
        // 5.列表删掉
        // 6.字节数删掉
        if (list_.empty() == true)
            return std::nullopt;
        auto it = std::prev(list_.end());
        auto temp = *it;
        bytes_ -= SizeOf(*it);
        cache_.erase(it->key_);
        list_.erase(it);
        return temp;
    }

    void LRUCache::Set(const std::string &key, const ByteView &value)
    {
        // 1.先查是否在cache中，
        // 2.如果在，则更新value，同时放到队首，改变bytes
        // 3.如果不在，则创建entry对象，放到队首，放入cache。
        // 4.最后统一看bytes是否超了
        std::vector<Entry> remove_entries;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = cache_.find(key);
            if (it != cache_.end())
            {
                bytes_ -= SizeOf(*(it->second));
                it->second->value_ = value;
                list_.splice(list_.begin(), list_, it->second);
                bytes_ += SizeOf(*(it->second));
            }
            else
            {
                list_.emplace_front(key, value);
                cache_[key] = list_.begin();
                bytes_ += SizeOf(*(list_.begin()));
            }

            while (max_bytes_ > 0 && bytes_ > max_bytes_)
            {
                remove_entries.emplace_back(std::move(*RemoveOldestLocked()));
            }
        }
        if (evicted_func_)
        {
            for (auto &removed : remove_entries)
            {
                evicted_func_(removed.key_, removed.value_);
            }
        }
    }

    void LRUCache::Delete(const std::string &key)
    {
        // 1.若不存在，直接返回
        // 2.若存在，map删list删bytes删
        std::optional<Entry> removed;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = cache_.find(key);
            if (it == cache_.end())
                return;
            bytes_ -= SizeOf(*it->second);
            removed = *it->second;
            list_.erase(it->second);
            cache_.erase(it);
        }
        if (evicted_func_)
        {
            evicted_func_(removed->key_, removed->value_);
        }
    }

    void LRUCache::RemoveOldest()
    {
        std::optional<Entry> removed;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            removed = RemoveOldestLocked();
        }
        if (removed && evicted_func_)
        {
            evicted_func_(removed->key_, removed->value_);
        }
    }
} // namespace kcache