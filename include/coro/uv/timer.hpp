/**
* LibUV Extension
* for better event loops and cross-platform support.
*/

#pragma once
#include <coroutine>

extern "C" {
#include <uv.h>
}

namespace coro {
    class timer_awaiter {
    public:
        timer_awaiter(uv_loop_t* loop, uint64_t timeout) : m_loop(loop), m_timeout(timeout) {}

        auto await_ready() -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> handle) -> void;

        auto await_resume() -> void {}

    private:
        struct timer_context {
            uv_timer_t timer {};
            std::coroutine_handle<> handle;

            explicit timer_context(std::coroutine_handle<> handle) : handle(handle) {}
        };

        uv_loop_t* m_loop = nullptr;
        uint64_t m_timeout = 0;
    };
}
