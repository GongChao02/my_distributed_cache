#include "gcache/group.h"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
namespace gcache
{
    namespace
    {
        std::mutex registry_mtx;
        std::unordered_map<std::string, std::unique_ptr<CacheGroup>> groups;
    }

    CacheGroup &MakeCacheGroup(const std::string &name, std::int64_t bytes, DataGetter getter)
    {
        std::lock_guard<std::mutex> lock(registry_mtx);
        if (groups.find(name) != groups.end())
        {
            throw std::invalid_argument("duplicate cache group: " + name);
        }
        auto group = std::make_unique<CacheGroup>(name, bytes, std::move(getter));
        CacheGroup &result = *group;
        groups.emplace(name, std::move(group));
        return result;
    }

    CacheGroup *GetCacheGroup(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(registry_mtx);
        const auto it = groups.find(name);
        return it == groups.end() ? nullptr : it->second.get();
    }
}