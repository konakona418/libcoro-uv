#include "coro/uv/ip_address.hpp"

#include <stdexcept>
#include <uv.h>

namespace coro::uv {
    auto ip_address::from_string(const std::string& ip_address) -> uv::ip_address {
        auto it = ip_address.find(':');
        if (it == std::string::npos) {
            throw std::invalid_argument("Invalid IP address");
        }
        std::string host = ip_address.substr(0, it);
        return {std::move(host), static_cast<uint16_t>(std::stoi(ip_address.substr(it + 1)))};
    }
}
