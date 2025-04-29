/**
* LibUV Extension
* for better event loops and cross-platform support.
*/


#include "coro/uv_scheduler.hpp"

#include "coro/uv/timer.hpp"

namespace coro {
    auto uv_scheduler::operation::await_suspend(std::coroutine_handle<> handle) const -> void {
        m_scheduler.m_thread_pool->resume(handle);
    }

    auto uv_scheduler::make_shared(const coro::uv_thread_pool::options& opts) -> std::shared_ptr<uv_scheduler> {
        return std::make_shared<uv_scheduler>(opts);
    }


    auto uv_scheduler::yield_for(std::chrono::milliseconds timeout) -> coro::task<> {
        if (timeout <= std::chrono::milliseconds { 0 }) {
            co_await schedule();
        }
        auto* uv_loop = m_thread_pool->get_raw_loop();

        auto awaiter = coro::timer_awaiter { uv_loop, static_cast<uint64_t>(timeout.count()) };
        co_await awaiter;

        co_return;
    }


}
