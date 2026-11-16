#include "include/gcache/singleflight.h"

#include <exception>
#include <utility>

namespace gcache
{

    SingleFlight::Result SingleFlight::Do(const std::string &key, const Func &func)
    {
        std::shared_ptr<Call> call;
        bool is_leader = false;

        {
            std::lock_guard<std::mutex> lock(mtx_);
            const auto it = in_flight_.find(key);
            if (it != in_flight_.end())
            {
                call = it->second;
            }
            else
            {
                call = std::make_shared<Call>();
                in_flight_.emplace(key, call);
                is_leader = true;
            }
        }

        if (!is_leader)
        {
            return call->future.get();
        }

        try
        {
            Result result = func();
            call->promise.set_value(result);
            {
                std::lock_guard<std::mutex> lock(mtx_);
                in_flight_.erase(key);
            }
            return result;
        }
        catch (...)
        {
            auto error = std::current_exception();
            try
            {
                call->promise.set_exception(error);
            }
            catch (const std::future_error &)
            {
                // 若共享状态之前已经完成，不重复设置结果，保持原状态。
            }
            {
                std::lock_guard<std::mutex> lock(mtx_);
                in_flight_.erase(key);
            }
            std::rethrow_exception(error);
        }
    }

} // namespace gcache