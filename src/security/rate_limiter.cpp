#include "security/rate_limiter.h"

namespace lan_transfer {

bool RateLimiter::allow(const std::string& key,
                        std::size_t max_requests,
                        std::chrono::steady_clock::duration window) {
    if (key.empty() || max_requests == 0) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    auto& history = request_history_[key];
    while (!history.empty() && now - history.front() > window) {
        history.pop_front();
    }

    if (history.size() >= max_requests) {
        return false;
    }

    history.push_back(now);
    return true;
}

}  // namespace lan_transfer
