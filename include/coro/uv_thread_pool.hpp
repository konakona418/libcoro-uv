/**
 * LibUV Extension
 * for better event loops and cross-platform support.
 */

#pragma once

#include <condition_variable>

#include "coro/concepts/range_of.hpp"
#include "task.hpp"

#include <coroutine>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

extern "C" {
#include <uv.h>
}


namespace coro {
    class uv_thread_pool {
    public:
        class operation {
            friend class uv_thread_pool;
            explicit operation(uv_thread_pool& pool) noexcept;

        public:
            auto await_ready() noexcept -> bool {
                return false;
            };

            auto await_suspend(std::coroutine_handle<> handle) noexcept -> void;

            auto await_resume() noexcept -> void {};

        private:
            uv_thread_pool& m_thread_pool;
            std::coroutine_handle<> m_awaiting_coroutine { nullptr };
        };

        struct options {
            std::function<void(size_t thread_id)> on_worker_start = nullptr;
            std::function<void(size_t thread_id)> on_worker_stop  = nullptr;

            static auto defaults() -> options {
                options opt {};
                return opt;
            }
        };

        explicit uv_thread_pool(options opts = options::defaults()) noexcept;

        uv_thread_pool(const uv_thread_pool&) = delete;
        uv_thread_pool(uv_thread_pool&&)      = delete;

        auto operator=(const uv_thread_pool&) -> uv_thread_pool& = delete;
        auto operator=(uv_thread_pool&&)      -> uv_thread_pool& = delete;

        virtual ~uv_thread_pool();

        [[nodiscard]] auto schedule() -> operation;

        auto spawn(coro::task<void>&& task) noexcept -> bool;

        template <typename return_type>
        [[nodiscard]] auto schedule(coro::task<return_type> task) -> coro::task<return_type> {
            co_await schedule();
            co_return co_await task;
        }

        auto resume(std::coroutine_handle<> handle) noexcept -> bool;

        template<coro::concepts::range_of<std::coroutine_handle<>> range_type>
        auto resume(const range_type& handles) noexcept -> uint64_t {
            m_size.fetch_add(std::size(handles), std::memory_order::release);

            size_t null_handles = 0;

            {
                std::scoped_lock lock { m_wait_mutex };
                for (const auto& handle : handles) {
                    if (handle != nullptr) [[likely]] {
                        m_queue.emplace_back(handle);
                    } else {
                        ++null_handles;
                    }
                }
            }

            if (null_handles > 0) {
                m_size.fetch_sub(null_handles, std::memory_order::release);
            }

            const size_t total = std::size(handles) - null_handles;
            /*if (total >= m_threads.size()) {
                m_wait_condition.notify_all();
            } else {
                for (size_t i = 0; i < total; ++i) {
                    m_wait_condition.notify_one();
                }
            }*/
            m_wait_condition.notify_all();

            return total;
        }

        [[nodiscard]] auto yield() -> operation {
            return schedule();
        }

        auto shutdown() noexcept -> void;

        [[nodiscard]] auto size() const noexcept -> size_t {
            return m_size.load(std::memory_order::acquire);
        }

        [[nodiscard]] auto empty() const noexcept -> bool {
            return size() == 0;
        }

        [[nodiscard]] auto queue_size() const noexcept -> std::size_t {
            std::atomic_thread_fence(std::memory_order::acquire);
            return m_queue.size();
        }

        [[nodiscard]] auto queue_empty() const noexcept -> bool {
            return queue_size() == 0;
        }

        /* issue with this.
         * this problem is concerned with LibUV not me.
         * which uses getenv() to get the thread pool size directly,
         * rather than using uv_os_getenv()
         */
        static auto set_uv_thread_pool_size(size_t size) -> void {
            uv_os_setenv("UV_THREADPOOL_SIZE", std::to_string(size).c_str());
        }

    private:
        class uv_work {
        public:
            uv_work(uv_work_cb work_cb, uv_after_work_cb after_work_cb) noexcept;
            ~uv_work() noexcept = default;

            uv_work_cb m_work_cb;
            uv_after_work_cb m_after_work_cb;
        };

        struct work_context {
            std::coroutine_handle<> handle;
            uv_thread_pool* thread_pool;
            uv_work_t work {};

            work_context(std::coroutine_handle<> handle, uv_thread_pool* thread_pool) noexcept : handle(handle), thread_pool(thread_pool) {};
        };

        class uv_loop {
        public:
            uv_loop() noexcept;
            ~uv_loop() noexcept;

            auto run(uv_run_mode run_mode) const -> void;
            auto stop() const -> void;
            auto close() const -> void;

            [[nodiscard]] auto queue_work(uv_work work, uv_work_t* real_work) const -> int;
        private:
            uv_loop_t* m_loop;
        };

        options m_options;
        std::thread m_worker;
        uv_loop m_loop;

        std::mutex m_wait_mutex;
        std::condition_variable m_wait_condition;
        std::deque<std::coroutine_handle<>> m_queue;

        auto executor(size_t idx) -> void;

        auto schedule_impl(std::coroutine_handle<> handle) noexcept -> void;

        std::atomic<size_t> m_size { 0 };
        std::atomic<bool> m_shutdown { false };
    };
}
