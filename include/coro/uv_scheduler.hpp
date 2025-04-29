/**
* LibUV Extension
* for better event loops and cross-platform support.
*/

#pragma once

#include <coroutine>
#include <memory>
#include <chrono>

#include "uv_thread_pool.hpp"

namespace coro {
    class uv_scheduler : std::enable_shared_from_this<uv_scheduler> {
    public:
        class operation {
        public:
            friend class uv_scheduler;

            explicit operation(uv_scheduler& scheduler) : m_scheduler(scheduler) {}

            auto await_ready() -> bool { return false; }

            auto await_suspend(std::coroutine_handle<> handle) const -> void;

            auto await_resume() -> void {};

        private:
            uv_scheduler& m_scheduler;
        };

        explicit uv_scheduler(const coro::uv_thread_pool::options& opts)
            : m_thread_pool(std::make_unique<uv_thread_pool>(opts)) {}

        ~uv_scheduler() { m_thread_pool->shutdown(); }

        static auto make_shared(const coro::uv_thread_pool::options& opts) -> std::shared_ptr<uv_scheduler>;

        auto schedule() -> operation { return operation(*this); }

        auto spawn(coro::task<void>&& task) const noexcept -> bool { return m_thread_pool->spawn(std::move(task)); }

        auto yield() -> operation { return operation(*this); }

        auto yield_for(std::chrono::milliseconds timeout) -> coro::task<>;

        auto shutdown() -> void { m_thread_pool->shutdown(); }

    private:
        std::unique_ptr<uv_thread_pool> m_thread_pool;
    };

}
