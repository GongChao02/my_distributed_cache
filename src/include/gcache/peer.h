#pragma once

#include "cache.h"

#include <string>
#include <vector>

namespace gcache
{
    enum class SyncFlag
    {
        SET,
        DELETE,
        INVALIDATE
    };

    class Peer
    {
    public:
        virtual ~Peer() = default;
        virtual ByteViewOptional Get(const std::string &group, const std::string &key) = 0;
        virtual bool Set(const std::string &group, const std::string &key, const ByteView &value) = 0;
        virtual bool Delete(const std::string &group, const std::string &key) = 0;
        virtual bool Invalidate(const std::string &group, const std::string &key) = 0;
    };

    class PeerPicker
    {
    public:
        virtual ~PeerPicker() = default;
        virtual Peer *PickPeer(const std::string &key) = 0;
        virtual std::vector<Peer *> GetAllPeers() = 0;
    };
}