#pragma once

#include <cstdint>
#include <string>

#include "common/protocol.h"
#include "pairing/pair_store.h"

namespace lan_transfer {

class Socket;

class PairingManager {
public:
    explicit PairingManager(PairStore& pair_store);

    void initiate_pairing(const std::string& host, std::uint16_t port) const;
    void handle_incoming_pair_request(Socket& socket,
                                      ProtocolCodec& codec,
                                      const MessageFrame& initial_frame) const;

private:
    PairStore& pair_store_;

    static bool prompt_for_confirmation(const std::string& question);
};

}  // namespace lan_transfer
