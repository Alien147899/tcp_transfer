#include "pairing/pairing_manager.h"

#include <iostream>
#include <stdexcept>

#include "net/socket.h"

namespace lan_transfer {

PairingManager::PairingManager(PairStore& pair_store) : pair_store_(pair_store) {}

void PairingManager::initiate_pairing(const std::string& host, std::uint16_t port) const {
    const std::string local_device_id = pair_store_.get_or_create_local_device_id();
    if (!prompt_for_confirmation("Confirm pairing request to " + host + ":" + std::to_string(port) + "? [y/N]: ")) {
        throw std::runtime_error("pairing cancelled by local user");
    }

    Socket socket;
    socket.connect_to(host, port);

    ProtocolCodec codec;
    codec.send_frame(socket, make_pair_request_frame(PairRequest{local_device_id}));

    const MessageFrame response_frame = codec.receive_frame(socket);
    if (response_frame.type == MessageType::PairReject) {
        throw std::runtime_error("pairing rejected: " + parse_pair_reject_frame(response_frame));
    }
    if (response_frame.type == MessageType::Error) {
        throw std::runtime_error("peer reported error: " + parse_error_frame(response_frame));
    }
    if (response_frame.type != MessageType::PairAccept) {
        throw std::runtime_error("unexpected response to pairing request");
    }

    const PairAccept accept = parse_pair_accept_frame(response_frame);
    if (!prompt_for_confirmation("Remote device " + accept.accepter_device_id +
                                 " accepted. Finalize pairing? [y/N]: ")) {
        codec.send_frame(socket, make_pair_reject_frame("initiator cancelled pairing"));
        throw std::runtime_error("pairing cancelled by local user");
    }

    codec.send_frame(socket, make_pair_finalize_frame(PairFinalize{local_device_id, accept.accepter_device_id}));
    pair_store_.upsert_paired_device(PairedDevice{accept.accepter_device_id, host, port});
}

void PairingManager::handle_incoming_pair_request(Socket& socket,
                                                  ProtocolCodec& codec,
                                                  const MessageFrame& initial_frame) const {
    const PairRequest request = parse_pair_request_frame(initial_frame);
    const std::string local_device_id = pair_store_.get_or_create_local_device_id();

    if (!prompt_for_confirmation("Allow pairing with device " + request.requester_device_id + "? [y/N]: ")) {
        codec.send_frame(socket, make_pair_reject_frame("pairing rejected by receiver"));
        return;
    }

    codec.send_frame(socket, make_pair_accept_frame(PairAccept{local_device_id}));

    const MessageFrame final_frame = codec.receive_frame(socket);
    if (final_frame.type == MessageType::PairReject) {
        throw std::runtime_error("initiator cancelled pairing: " + parse_pair_reject_frame(final_frame));
    }
    if (final_frame.type == MessageType::Error) {
        throw std::runtime_error("peer reported error: " + parse_error_frame(final_frame));
    }
    if (final_frame.type != MessageType::PairFinalize) {
        throw std::runtime_error("expected pair finalize frame");
    }

    const PairFinalize finalize = parse_pair_finalize_frame(final_frame);
    if (finalize.requester_device_id != request.requester_device_id) {
        throw std::runtime_error("pair finalize requester does not match original request");
    }
    if (finalize.accepter_device_id != local_device_id) {
        throw std::runtime_error("pair finalize accepter does not match local device");
    }

    pair_store_.upsert_paired_device(PairedDevice{request.requester_device_id, "", 0});
    std::cout << "Pairing completed with device: " << request.requester_device_id << '\n';
}

bool PairingManager::prompt_for_confirmation(const std::string& question) {
    std::cout << question << '\n' << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

}  // namespace lan_transfer
