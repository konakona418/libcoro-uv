/**
 * LibUV Extension
 * for better event loops and cross-platform support.
 */

#pragma once

#include "coro/uv_thread_pool.hpp"
#include "coro/detail/task_self_deleting.hpp"

namespace coro {
    uv_thread_pool::operation::operation(uv_thread_pool& pool) noexcept : m_thread_pool(pool) {

    }

    auto uv_thread_pool::operation::await_suspend(std::coroutine_handle<> handle) noexcept -> void {
        m_awaiting_coroutine = handle;
        m_thread_pool.schedule_impl(m_awaiting_coroutine);
    }

    uv_thread_pool::uv_thread_pool(options opts) noexcept : m_options(std::move(opts)) {
        m_worker = std::thread([this]() {
            executor(0);
        });

        uv_async_init(m_loop.get_loop(), &m_main_loop_close_handle, [](uv_async_t* handle) {
            uv_stop(handle->loop);
        });
        uv_unref(reinterpret_cast<uv_handle_t*>(&m_main_loop_close_handle));

        m_main_loop = std::thread([this]() {
            while (!m_shutdown.load(std::memory_order::acquire)) {
                m_loop.run(UV_RUN_DEFAULT);
            }
        });
    }

    uv_thread_pool::~uv_thread_pool() noexcept {
        shutdown();
    }

    auto uv_thread_pool::schedule() -> operation {
        m_size.fetch_add(1, std::memory_order::release);
        if (!m_shutdown.load(std::memory_order::acquire)) {
            return operation(*this);
        }
        m_size.fetch_sub(1, std::memory_order::release);
        throw std::runtime_error("coro::uv_thread_pool is shutting down, unable to schedule new tasks.");
    }

    auto uv_thread_pool::spawn(coro::task<void>&& task) noexcept -> bool {
        m_size.fetch_add(1, std::memory_order::release);
        auto wrapper_task = detail::make_task_self_deleting(std::move(task));
        wrapper_task.promise().executor_size(m_size);
        return resume(wrapper_task.handle());
    }

    auto uv_thread_pool::resume(std::coroutine_handle<> handle) noexcept -> bool {
        if (handle == nullptr || handle.done()) {
            return false;
        }

        m_size.fetch_add(1, std::memory_order::release);
        if (m_shutdown.load(std::memory_order::acquire)) {
            m_size.fetch_sub(1, std::memory_order::release);
            return false;
        }
        schedule_impl(handle);

        return true;
    }


    auto uv_thread_pool::shutdown() noexcept -> void {
        if (m_shutdown.exchange(true, std::memory_order::acq_rel) == false) {
            {
                std::unique_lock lock { m_wait_mutex };
                m_wait_condition.notify_all();
            }

            if (m_worker.joinable()) {
                m_worker.join();
            }

            uv_async_send(&m_main_loop_close_handle);
            m_main_loop.join();
        }
    }

    auto uv_thread_pool::executor(size_t idx) -> void {
        if (m_options.on_worker_start != nullptr) {
            m_options.on_worker_start(idx);
        }

        const auto worker = [this](std::unique_lock<std::mutex> lock) {
            auto handle = m_queue.front();
            m_queue.pop_front();
            lock.unlock();

            auto* ctx = new work_context(handle, this);

            uv_work work(
            [](uv_work_t* work) {
                auto* ctx = static_cast<work_context *>(work->data);
                ctx->handle.resume();
                ctx->thread_pool->m_size.fetch_sub(1, std::memory_order::release);
            },
            [](uv_work_t* work, int status) {
                auto* ctx = static_cast<work_context *>(work->data);
                delete ctx;
            });
            ctx->work.data = static_cast<void *>(ctx);

            lock.lock();
            if (!m_loop.queue_work(work, &ctx->work)) {
                // TODO: handle error
            }
            lock.unlock();
        };

        while (!m_shutdown.load(std::memory_order::acquire)) {
            std::unique_lock lock { m_wait_mutex };
            m_wait_condition.wait(lock, [this]() {
                if (m_shutdown.load(std::memory_order::acquire)) {
                    return true;
                }
                return !m_queue.empty();
            });

            if (m_shutdown.load(std::memory_order::acquire)) {
                break;
            }

            if (m_queue.empty()) {
                continue;
            }

            worker(std::move(lock));
        }

        while (m_size.load(std::memory_order::acquire) > 0) {
            std::unique_lock lock { m_wait_mutex };
            if (m_queue.empty()) {
                break;
            }

            worker(std::move(lock));
        }

        if (m_options.on_worker_stop != nullptr) {
            m_options.on_worker_stop(idx);
        }
    }

    auto uv_thread_pool::schedule_impl(std::coroutine_handle<> handle) noexcept -> void {
        if (handle == nullptr || handle.done()) {
            return;
        }

        {
            std::scoped_lock lock { m_wait_mutex };
            m_queue.emplace_back(handle);
            m_wait_condition.notify_one();
        }
    }

    uv_thread_pool::uv_work::uv_work(uv_work_cb work_cb, uv_after_work_cb after_work_cb) noexcept {
        m_work_cb = work_cb;
        m_after_work_cb = after_work_cb;
    }

    uv_thread_pool::uv_loop::uv_loop() noexcept {
        m_loop = new uv_loop_t;
        uv_loop_init(m_loop);
    }

    uv_thread_pool::uv_loop::~uv_loop() noexcept {
        close();
        delete m_loop;
    }

    auto uv_thread_pool::uv_loop::run(uv_run_mode run_mode = UV_RUN_DEFAULT) const -> void {
        uv_run(m_loop, run_mode);
    }

    auto uv_thread_pool::uv_loop::stop() const -> void {
        uv_stop(m_loop);
    }

    auto uv_thread_pool::uv_loop::close() const -> void {
        uv_loop_close(m_loop);
    }

    auto uv_thread_pool::uv_loop::queue_work(uv_work work, uv_work_t* real_work) const -> int {
        return uv_queue_work(m_loop, real_work, work.m_work_cb, work.m_after_work_cb);
    }


}

