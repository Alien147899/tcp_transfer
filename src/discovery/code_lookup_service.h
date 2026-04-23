#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "pairing/pair_store.h"

namespace lan_transfer {

class CodeLookupService {
public:
    explicit CodeLookupService(PairStore& pair_store);
    ~CodeLookupService();

    CodeLookupService(const CodeLookupService&) = delete;
    CodeLookupService& operator=(const CodeLookupService&) = delete;

    void start(std::uint16_t listen_port);
    void stop();

private:
    void run(std::uint16_t listen_port);

    PairStore& pair_store_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace lan_transfer
