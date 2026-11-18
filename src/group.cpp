#include "include/gcache/group.h"

#include <stdexcept>
#include <utility>

namespace gcache
{

    CacheGroup::CacheGroup(std::string name, std::int64_t bytes, DataGetter getter) : cache_(std::make_unique<LRUCache>(bytes)), name_(std::move(name)),
                                                                                      getter_(std::move(getter))
    {
        if (name_.empty())
            throw std::invalid_argument("group name must not be empty");

        if (!getter_)
            throw std::invalid_argument("DataGetter must not be empty");
    }

    void CacheGroup::RegisterPeerPicker(std::unique_ptr<PeerPicker> &&picker)
    {
        if (!picker)
            throw std::invalid_argument("peer picker must not be null");

        if (peer_picker_)
            throw std::logic_error("peer picker already registered");

        peer_picker_ = std::move(picker);
    }

    ByteViewOptional CacheGroup::Get(const std::string &key)
    {
        if (is_close_.load() || key.empty())
            return std::nullopt;
        auto value = cache_->Get(key);
        if (value)
        {
            ++status_.local_hits;
            return value;
        }
        ++status_.local_misses;
        return Load(key);
    }

    ByteViewOptional CacheGroup::Load(const std::string &key)
    {
        return loader_.Do(key, [this, &key]() -> ByteViewOptional
                          {
                            // SingleFlight leader 再查一次，避免等待 leader 的间隙重复回源。
                            if (auto existing = cache_->Get(key)) return existing;
                            auto value = LoadData(key);
                            if (value) cache_->Set(key, *value);
                            return value; });
    }

    ByteViewOptional CacheGroup::LoadData(const std::string &key)
    {
        if (peer_picker_)
        {
            if (Peer *peer = peer_picker_->PickPeer(key))
            {
                try
                {
                    if (auto value = LoadFromPeer(peer, key))
                    {
                        ++status_.peer_hits;
                        return value;
                    }
                }
                catch (const std::exception &)
                {
                    // 远程不可用时，按原文的分层查找策略回退到数据源。
                }
                ++status_.peer_misses;
            }
        }
        auto value = getter_(key);
        if (value)
            ++status_.source_hits;
        else
            ++status_.source_misses;
        return value;
    }

    ByteViewOptional CacheGroup::LoadFromPeer(Peer *peer, const std::string &key)
    {
        return peer->Get(name_, key);
    }

    bool CacheGroup::Set(const std::string &key, const ByteView &value, bool is_from_peer)
    {
        if (is_close_.load() || key.empty())
            return false;
        cache_->Set(key, value);
        if (!is_from_peer && peer_picker_)
            SyncToPeers(key, SyncFlag::SET, value);
        return true;
    }

    bool CacheGroup::Delete(const std::string &key, bool is_from_peer)
    {
        if (is_close_.load() || key.empty())
            return false;
        cache_->Delete(key);
        if (!is_from_peer && peer_picker_)
            SyncToPeers(key, SyncFlag::DELETE, ByteView(""));
        return true;
    }

    bool CacheGroup::Invalidate(const std::string &key, bool is_from_peer)
    {
        if (is_close_.load() || key.empty())
            return false;
        cache_->Delete(key);
        if (!is_from_peer && peer_picker_)
            SyncToPeers(key, SyncFlag::INVALIDATE, ByteView(""));
        return true;
    }

    void CacheGroup::SyncToPeers(const std::string &key, SyncFlag op, const ByteView &value)
    {
        if (!peer_picker_)
            return;
        if (op == SyncFlag::SET)
        {
            Peer *primary = peer_picker_->PickPeer(key);
            // 主节点是本地时，primary 为 nullptr，无须远程 Set。
            if (primary)
            {
                try
                {
                    (void)primary->Set(name_, key, value);
                }
                catch (const std::exception &)
                {
                }
            }
            for (Peer *peer : peer_picker_->GetAllPeers())
            {
                if (peer && peer != primary)
                {
                    try
                    {
                        (void)peer->Invalidate(name_, key);
                    }
                    catch (const std::exception &)
                    {
                    }
                }
            }
        }
        else if (op == SyncFlag::DELETE)
        {
            for (Peer *peer : peer_picker_->GetAllPeers())
            {
                if (peer)
                {
                    try
                    {
                        (void)peer->Delete(name_, key);
                    }
                    catch (const std::exception &)
                    {
                    }
                }
            }
        }
        else if (op == SyncFlag::INVALIDATE)
        {
            for (Peer *peer : peer_picker_->GetAllPeers())
            {
                if (peer)
                {
                    try
                    {
                        (void)peer->Invalidate(name_, key);
                    }
                    catch (const std::exception &)
                    {
                    }
                }
            }
        }
    }

    GroupStatusSnapshot CacheGroup::GetStatus() const
    {
        GroupStatusSnapshot s;
        s.local_hits = status_.local_hits.load();
        s.local_misses = status_.local_misses.load();
        s.peer_hits = status_.peer_hits.load();
        s.peer_misses = status_.peer_misses.load();
        s.source_hits = status_.source_hits.load();
        s.source_misses = status_.source_misses.load();
        return s;
    }

}