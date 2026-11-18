#pragma once

#include "cache.h"
#include "peer.h"
#include "singleflight.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace gcache
{

    using DataGetter = std::function<ByteViewOptional(const std::string &key)>;

    struct GroupStatusSnapshot
    {
        std::uint64_t local_hits = 0;
        std::uint64_t local_misses = 0;
        std::uint64_t peer_hits = 0;
        std::uint64_t peer_misses = 0;
        std::uint64_t source_hits = 0;
        std::uint64_t source_misses = 0;
    };

    class CacheGroup
    {
    public:
        CacheGroup(std::string name, std::int64_t bytes, DataGetter getter);
        ~CacheGroup() = default;

        CacheGroup(const CacheGroup &) = delete;
        CacheGroup &operator=(const CacheGroup &) = delete;
        CacheGroup(CacheGroup &&) = delete;
        CacheGroup &operator=(CacheGroup &&) = delete;

        ByteViewOptional Get(const std::string &key);
        bool Set(const std::string &key, const ByteView &value, bool is_from_peer = false);
        bool Delete(const std::string &key, bool is_from_peer = false);
        bool Invalidate(const std::string &key, bool is_from_peer = true);

        void RegisterPeerPicker(std::unique_ptr<PeerPicker> &&picker);
        void SyncToPeers(const std::string &key, SyncFlag op,
                         const ByteView &value);
        GroupStatusSnapshot GetStatus() const;
        const std::string &Name() const noexcept { return name_; }
        void Close() noexcept { is_close_.store(true); }

    private:
        ByteViewOptional Load(const std::string &key);
        ByteViewOptional LoadData(const std::string &key);
        ByteViewOptional LoadFromPeer(Peer *peer, const std::string &key);

        struct GroupStatus
        {
            std::atomic<std::uint64_t> local_hits{0};
            std::atomic<std::uint64_t> local_misses{0};
            std::atomic<std::uint64_t> peer_hits{0};
            std::atomic<std::uint64_t> peer_misses{0};
            std::atomic<std::uint64_t> source_hits{0};
            std::atomic<std::uint64_t> source_misses{0};
        };

        std::unique_ptr<LRUCache> cache_;
        std::unique_ptr<PeerPicker> peer_picker_;
        std::string name_;
        DataGetter getter_;
        SingleFlight loader_;
        std::atomic<bool> is_close_{false};
        GroupStatus status_;
    };

    // 供后续 gRPC server 根据 group 名称获取缓存组。
    CacheGroup &MakeCacheGroup(const std::string &name,
                               std::int64_t bytes, DataGetter getter);
    CacheGroup *GetCacheGroup(const std::string &name);

}