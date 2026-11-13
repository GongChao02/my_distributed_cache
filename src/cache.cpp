#include "./include/gcache/cache.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gcache
{
    ByteView::ByteView(const std::string &str) : data_(str.begin(), str.end()) {}
    std::int64_t ByteView::Len() const { return static_cast<std::int64_t>(data_.size()); }
    std::string ByteView::ToString() const { return std::string(data_.begin(), data_.end()); }

    struct Entry
    {
        std::string key_;
        ByteView value_;

        Entry(std::string key, const ByteView &value);

        bool operator==(const Entry &other) const;
    };

    Entry::Entry(std::string key, const ByteView &value) : key_(key), value_(value) {};
    bool Entry::operator==(const Entry &other) const
    {
        return key_ == other.key_ && value_.ToString() == other.value_.ToString();
    }
} // namespace kcache