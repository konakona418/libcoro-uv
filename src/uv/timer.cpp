/**
* LibUV Extension
* for better event loops and cross-platform support.
*/

#include "coro/uv/timer.hpp"

namespace coro {
    auto timer_awaiter::await_suspend(std::coroutine_handle<> handle) -> void {
        auto* ctx = new timer_context(handle);

        ctx->timer.data = ctx;

        uv_timer_init(m_loop, &ctx->timer);
        uv_timer_start(&ctx->timer, [](uv_timer_t* handle) {
            auto* ctx = static_cast<timer_context *>(handle->data);
            uv_timer_stop(handle);

            ctx->handle.resume();
            uv_close(reinterpret_cast<uv_handle_t*>(handle), [](uv_handle_t* handle) {
                delete static_cast<timer_context *>(handle->data);
            });
        }, m_timeout, 0);
    }

}