#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

} // namespace gcache