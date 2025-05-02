#pragma once

#include <cassert>
#include <coroutine>
#include <memory>

#include "coro/uv_scheduler.hpp"
#include "coro/uv/ip_address.hpp"

extern "C" {
#include <uv.h>
}

#define DEFAULT_BACKLOG 128

namespace coro::uv::tcp {
    template <typename T>
    using tcp_result = std::variant<T, std::exception_ptr>;

    class client {
        uv_tcp_t* m_uv_client;
        std::shared_ptr<coro::uv_scheduler> m_scheduler;
        bool m_closed { false }; // todo: may not be thread safe
    public:
        class read_awaiter {
        public:
            friend class client;

            explicit read_awaiter(client& client, uint8_t* buf, size_t buf_size)
                : m_client(client), m_buf(buf), m_buf_size(buf_size) {}

            auto await_ready() -> bool { return false; }

            auto await_suspend(std::coroutine_handle<> handle) -> void;

            auto await_resume() -> std::variant<size_t, std::exception_ptr>;

        private:
            client& m_client;
            std::coroutine_handle<> m_handle { nullptr };
            std::variant<size_t, std::exception_ptr> m_result;

            size_t m_buf_size { 0 };
            size_t m_bytes_read { 0 };

            uint8_t* m_buf { nullptr };
        };

        class write_awaiter {
        public:
            friend class client;

            explicit write_awaiter(client& client, uint8_t* buf, size_t buf_size)
                : m_client(client), m_buf(buf), m_buf_size(buf_size) {}

            auto await_ready() -> bool { return false; }

            auto await_suspend(std::coroutine_handle<> handle) -> void;

            auto await_resume() -> std::variant<size_t, std::exception_ptr>;

        private:
            struct write_context {
                write_awaiter* awaiter;
                uv_buf_t buf;
            };

            client& m_client;
            std::coroutine_handle<> m_handle { nullptr };
            std::variant<size_t, std::exception_ptr> m_result;

            size_t m_buf_size { 0 };
            size_t m_bytes_written { 0 };
            uint8_t* m_buf { nullptr };
        };

        explicit client(std::shared_ptr<coro::uv_scheduler> scheduler)
            : m_scheduler(std::move(scheduler)) {
            m_uv_client = new uv_tcp_t;
            assert(uv_tcp_init(m_scheduler->get_raw_loop(), m_uv_client) == 0);
        }

        ~client() = default;

        auto read(uint8_t* buf, size_t buf_size) -> coro::task<std::variant<size_t, std::exception_ptr>>;

        auto write(uint8_t* buf, size_t buf_size) -> coro::task<std::variant<size_t, std::exception_ptr>>;

        auto close() -> void;

        auto _accept(uv_stream_t* server) -> int;

        [[nodiscard]] auto get_handle() const -> uv_tcp_t* { return m_uv_client; }

        auto set_handle(uv_tcp_t* handle) -> void { m_uv_client = handle; }
    };

    class server {
        class uv_tcp_poller;
    public:
        struct options {
            ip_address address;
            int backlog = 128;

            static auto defaults() -> options {
                return {
                    .address = uv::ip_address("0.0.0.0", 8080),
                    .backlog = DEFAULT_BACKLOG
                };
            }
        };

        class tcp_poll_awaiter {
        public:
            friend class uv_tcp_poller;
            explicit tcp_poll_awaiter(uv_tcp_poller& poller)
                : m_poller(poller) {}

            auto await_ready() -> bool { return false; }

            auto await_suspend(std::coroutine_handle<> handle) -> void;

            auto await_resume() -> uv::tcp::client;

        private:
            uv_tcp_poller& m_poller;
            std::optional<client> m_client = std::nullopt;
            std::coroutine_handle<> m_handle { nullptr };
        };

        explicit server(std::shared_ptr<coro::uv_scheduler> scheduler)
            : m_scheduler(std::move(scheduler)), m_poller(m_scheduler), m_options(options::defaults()) {}

        auto bind(const options& opts = options::defaults()) -> void {
            m_poller.bind(opts.address);
        }

        auto listen() const -> void { m_poller.listen(m_options.backlog); }

        auto poll() -> coro::task<client>;

        [[nodiscard]] auto get_scheduler() const -> std::shared_ptr<coro::uv_scheduler> { return m_scheduler; }

    private:
        class uv_tcp_poller {
        public:
            friend class tcp_poll_awaiter;

            explicit uv_tcp_poller(const std::shared_ptr<coro::uv_scheduler>& scheduler) {
                m_scheduler = scheduler;

                auto* loop = scheduler->get_raw_loop();
                m_handle = new uv_tcp_t;
                m_handle->data = this;
                uv_tcp_init(loop, m_handle);
            }

            ~uv_tcp_poller();

            [[nodiscard]] uv_tcp_t* get() const {
                return m_handle;
            }

            auto bind(const uv::ip_address& addr) -> void;

            auto listen(int backlog) const -> void;

            /* todo: may not be thread safe */
            [[nodiscard]] auto ready() const -> bool { return !m_client_buffer.empty(); }

            auto fetch_one() -> std::optional<client> {
                {
                    std::scoped_lock lock { m_deque_mutex };

                    if (m_client_buffer.empty()) {
                        return std::nullopt;
                    }

                    auto client = m_client_buffer.front();
                    m_client_buffer.pop_front();
                    return client;
                }
            }

            auto subscribe(const std::shared_ptr<tcp_poll_awaiter>& awaiter) -> void;

            uv_tcp_poller(const uv_tcp_poller&) = delete;
            uv_tcp_poller& operator=(const uv_tcp_poller&) = delete;
            uv_tcp_poller(uv_tcp_poller&&) = delete;
            uv_tcp_poller& operator=(uv_tcp_poller&&) = delete;
        private:
            uv_tcp_t* m_handle;
            uv::ip_address m_addr = uv::ip_address(0);

            std::deque<std::shared_ptr<tcp_poll_awaiter>> m_pollers {};
            std::deque<client> m_client_buffer {};
            std::mutex m_deque_mutex {};

            std::shared_ptr<coro::uv_scheduler> m_scheduler;

            auto _on_polling_event(std::variant<client, std::exception_ptr>&& poll_result) -> void;
        };

        std::shared_ptr<coro::uv_scheduler> m_scheduler;
        uv_tcp_poller m_poller;
        options m_options;
    };

    class tcp_connect_awaiter {
    public:
        auto await_ready() -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> handle) -> void;

        auto await_resume() -> std::variant<tcp::client, std::exception_ptr>;

        tcp_connect_awaiter(const std::shared_ptr<coro::uv_scheduler>& scheduler, const uv::ip_address& addr)
            : m_scheduler(scheduler), m_addr(addr), m_result(tcp::client(m_scheduler)) {
        }

    private:
        std::shared_ptr<coro::uv_scheduler> m_scheduler;
        uv::ip_address m_addr;
        tcp_result<tcp::client> m_result;
        std::coroutine_handle<> m_handle { nullptr };

        uv_connect_t m_connect_handle;
    };

    auto connect(const std::shared_ptr<coro::uv_scheduler>& scheduler, const uv::ip_address& addr) -> coro::task<tcp_result<tcp::client>>;
}