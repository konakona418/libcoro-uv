#pragma once
#include <cstdint>
#include <string>
#include <psdk_inc/_ip_types.h>

namespace coro::uv {
    struct ip_address {
        static auto from_string(const std::string& ip_address) -> uv::ip_address;

        ip_address(std::string host, uint16_t port) noexcept : m_host(std::move(host)), m_port(port) {}

        explicit ip_address(uint16_t port) noexcept : ip_address("0.0.0.0", port) {}

        ~ip_address() = default;

        [[nodiscard]] auto host() const noexcept -> std::string {  return m_host; }

        [[nodiscard]] auto port() const noexcept -> uint16_t { return m_port; }

        [[nodiscard]] auto is_invalid() const noexcept -> bool { return m_host.empty() || m_port == 0; }
    private:
        std::string m_host;
        uint16_t m_port;
    };
}
