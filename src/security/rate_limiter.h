#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>

namespace lan_transfer {

class RateLimiter {
public:
    bool allow(const std::string& key,
               std::size_t max_requests,
               std::chrono::steady_clock::duration window);

private:
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> request_history_;
};

}  // namespace lan_transfer
