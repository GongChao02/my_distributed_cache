#include "cache.h"

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace gcache
{
    class SingleFlight
    {
    public:
        using Result = ByteViewOptional;
        using Func = std::function<Result()>;

        Result Do(const std::string &key, const Func &func);

    private:
        struct Call
        {
            std::promise<Result> promise;
            std::shared_future<Result> future = promise.get_future().share();
        };

        std::mutex mtx_;
        std::unordered_map<std::string, std::shared_ptr<Call>> in_flight_;
    };
}