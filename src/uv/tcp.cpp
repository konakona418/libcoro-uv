#include "coro/uv/tcp.hpp"

#include <cassert>

namespace coro::uv {
    auto client::read_awaiter::await_suspend(std::coroutine_handle<> handle) -> void {
        m_handle = handle;
        if (m_client.m_closed) {
            auto exception_ptr = std::make_exception_ptr(std::runtime_error("client closed"));
            auto variant = std::variant<size_t, std::exception_ptr>(std::move(exception_ptr));

            // todo: handle exception
            //assert(m_client.m_scheduler->resume(m_handle));
            m_handle.resume();
            return;
        }

        m_client.m_uv_client->data = this;
        int result = uv_read_start(
            reinterpret_cast<uv_stream_t *>(m_client.m_uv_client),
            [](uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
                auto* awaiter = static_cast<read_awaiter *>(handle->data);

                buf->base = reinterpret_cast<char *>(awaiter->m_buf);
                buf->len = awaiter->m_buf_size;
                // yes we can use the suggested size, but I'll leave it for now
            }, [](uv_stream_t* stream, ssize_t n_read, const uv_buf_t* buf) {
                auto* awaiter = static_cast<read_awaiter *>(stream->data);
                if (n_read < 0) {
                    std::variant<size_t, std::exception_ptr> variant;
                    if (n_read == UV_EOF) {
                        auto exception_ptr = std::make_exception_ptr(std::runtime_error("end of file"));
                        variant = std::variant<size_t, std::exception_ptr>(std::move(exception_ptr));
                    } else {
                        auto exception_ptr = std::make_exception_ptr(std::runtime_error("read failed"));
                        variant = std::variant<size_t, std::exception_ptr>(std::move(exception_ptr));
                    }

                    awaiter->m_result = std::move(variant);

                    awaiter->m_handle.resume();
                    //awaiter->m_client.m_scheduler->resume(awaiter->m_handle);
                    return;
                }
                auto variant = std::variant<size_t, std::exception_ptr>(static_cast<size_t>(std::abs(n_read)));
                // no need to do abs() but why bother?
                awaiter->m_result = std::move(variant);

                awaiter->m_handle.resume();
                //awaiter->m_client.m_scheduler->resume(awaiter->m_handle);
            });
        assert(result == 0);
    }

    auto client::read_awaiter::await_resume() -> std::variant<size_t, std::exception_ptr> {
        int err = uv_read_stop(reinterpret_cast<uv_stream_t *>(m_client.m_uv_client));
        assert(err == 0);
        return m_result;
    }

    auto client::write_awaiter::await_suspend(std::coroutine_handle<> handle) -> void {
        m_handle = handle;

        auto* write_req = new uv_write_t;

        auto* ctx = new write_context;
        ctx->awaiter = this;
        ctx->buf = uv_buf_init(reinterpret_cast<char *>(m_buf), m_buf_size);

        write_req->data = ctx;

        uv_write(write_req, reinterpret_cast<uv_stream_t *>(m_client.m_uv_client),
                 &ctx->buf, 1,
                 [](uv_write_t* req, int status) {
                     auto* ctx = static_cast<write_context *>(req->data);

                     if (status != 0) {
                         auto exception_ptr = std::make_exception_ptr(std::runtime_error("write failed"));
                         auto variant = std::variant<size_t, std::exception_ptr>(std::move(exception_ptr));

                         ctx->awaiter->m_result = std::move(variant);
                         ctx->awaiter->m_handle.resume();
                         //ctx->awaiter->m_client.m_scheduler->resume(ctx->awaiter->m_handle);
                         return;
                     }

                     auto variant = std::variant<size_t, std::exception_ptr>(static_cast<size_t>(ctx->buf.len));
                     ctx->awaiter->m_result = std::move(variant);
                     ctx->awaiter->m_handle.resume();
                     //ctx->awaiter->m_client.m_scheduler->resume(ctx->awaiter->m_handle);

                     delete ctx;
                     delete req;
                 });
    }

    auto client::write_awaiter::await_resume() -> std::variant<size_t, std::exception_ptr> {
        return m_result;
    }

    auto client::_accept(uv_stream_t* server) -> int {
        const auto status = uv_accept(server, reinterpret_cast<uv_stream_t *>(m_uv_client));
        if (status != 0) {
            close();
            return status;
        }
        return 0;
    }

    auto client::read(uint8_t* buf, size_t buf_size) -> coro::task<std::variant<size_t, std::exception_ptr>> {
        co_return co_await read_awaiter(*this, buf, buf_size);
    }

    auto client::write(uint8_t* buf, size_t buf_size) -> coro::task<std::variant<size_t, std::exception_ptr>> {
        co_return co_await write_awaiter(*this, buf, buf_size);
    }

    auto client::close() -> void {
        if (m_closed) {
            return;
        }

        m_closed = true; // just suppose it has been closed, to prevent potential race
        uv_close(reinterpret_cast<uv_handle_t *>(m_uv_client), [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_tcp_t *>(handle);
        });
    }

    auto server::tcp_poll_awaiter::await_suspend(std::coroutine_handle<> handle) -> void {
        auto client = m_poller.fetch_one();

        assert(handle != nullptr);

        m_handle = handle;
        if (client.has_value()) {
            m_client = client;

            // todo: handle exception
            assert(m_poller.m_scheduler->resume(m_handle)); // resume on the scheduler
        }
    }

    auto server::tcp_poll_awaiter::await_resume() -> uv::client {
        if (m_client.has_value()) {
            return m_client.value();
        }
        throw std::runtime_error("accept failed");
    }

    auto server::poll() -> coro::task<uv::client> {
        auto poll_awaiter = std::make_shared<tcp_poll_awaiter>(m_poller);
        m_poller.subscribe(poll_awaiter);
        co_return co_await *poll_awaiter;
    }

    server::uv_tcp_poller::~uv_tcp_poller() {
        uv_close(reinterpret_cast<uv_handle_t *>(m_handle), [](uv_handle_t* handle) {
            delete reinterpret_cast<uv_tcp_t *>(handle);
        });
    }

    void server::uv_tcp_poller::bind(const uv::ip_address& addr) {
        m_addr = addr;

        sockaddr_in addr_in{};
        uv_ip4_addr(m_addr.host().c_str(), m_addr.port(), &addr_in);

        uv_tcp_bind(m_handle, reinterpret_cast<const sockaddr *>(&addr_in), 0);
    }

    void server::uv_tcp_poller::listen(int backlog = DEFAULT_BACKLOG) const {
        auto cb = [](uv_stream_t* server, int status) {
            if (status != 0) {
                return;
            }
            auto* handle = static_cast<uv_tcp_poller *>(server->data);
            client client(handle->m_scheduler);
            if (!client._accept(server)) {
                auto variant = std::variant<uv::client, std::exception_ptr>(std::move(client));
                handle->_on_polling_event(std::move(variant));
                return;
            }
            auto exception_ptr = std::make_exception_ptr(std::runtime_error("accept failed"));
            auto variant = std::variant<uv::client, std::exception_ptr>(std::move(exception_ptr));
            handle->_on_polling_event(std::move(variant));
        };
        uv_listen(reinterpret_cast<uv_stream_t *>(m_handle), backlog, cb);
    }

    auto server::uv_tcp_poller::subscribe(const std::shared_ptr<tcp_poll_awaiter>& awaiter) -> void {
        {
            std::scoped_lock lock{m_deque_mutex};
            m_pollers.push_back(awaiter);
        }
    }

    auto server::uv_tcp_poller::_on_polling_event(std::variant<client, std::exception_ptr>&& poll_result) -> void {
        {
            std::scoped_lock lock{m_deque_mutex};
            if (m_pollers.empty()) {
                m_client_buffer.push_back(std::move(std::get<client>(poll_result)));
            } else {
                auto poller = m_pollers.front();
                m_pollers.pop_front();

                // this means that poller is already resumed
                // or how can m_client has already been set?
                if (poller->m_client != std::nullopt) {
                    // however we need to push client to buffer, rather than simply dispose it
                    if (std::holds_alternative<client>(poll_result)) {
                        m_client_buffer.push_back(std::move(std::get<client>(poll_result)));
                    } else {
                        // todo: handle exception
                    }
                    return;
                }

                if (!std::holds_alternative<client>(poll_result)) {
                    poller->m_client = std::nullopt;
                    // poller->m_handle.resume();
                    m_scheduler->resume(poller->m_handle); // resume on the scheduler

                    return;
                }

                poller->m_client = std::move(std::get<client>(poll_result));
                //poller->m_handle.resume();
                m_scheduler->resume(poller->m_handle);
                // resume on the scheduler
                // do not resume the handle directly, or the uv_loop will suspend!
            }
        }
    }
}
