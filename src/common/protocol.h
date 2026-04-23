#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "common/types.h"

namespace lan_transfer {

class Socket;

enum class MessageType : std::uint8_t {
    PairRequest = 1,
    PairAccept = 2,
    PairReject = 3,
    PairFinalize = 4,
    FileRequest = 10,
    FileAccept = 11,
    FileReject = 12,
    FileChunk = 13,
    TransferComplete = 14,
    Error = 255
};

struct MessageFrame {
    std::uint8_t version = 1;
    MessageType type = MessageType::Error;
    std::vector<std::uint8_t> payload;
};

class ProtocolError : public std::runtime_error {
public:
    explicit ProtocolError(const std::string& message);
};

class ProtocolCodec {
public:
    static constexpr std::uint32_t kMagic = 0x4C465431;
    static constexpr std::uint8_t kVersion = 1;
    static constexpr std::uint32_t kMaxPayloadLength = 1024U * 1024U;

    void send_frame(Socket& socket, const MessageFrame& frame) const;
    MessageFrame receive_frame(Socket& socket) const;
};

MessageFrame make_file_request_frame(const FileRequest& request);
FileRequest parse_file_request_frame(const MessageFrame& frame);

MessageFrame make_file_accept_frame(const FileAccept& accept);
FileAccept parse_file_accept_frame(const MessageFrame& frame);

MessageFrame make_file_reject_frame(const std::string& reason);
std::string parse_file_reject_frame(const MessageFrame& frame);

MessageFrame make_pair_request_frame(const PairRequest& request);
PairRequest parse_pair_request_frame(const MessageFrame& frame);

MessageFrame make_pair_accept_frame(const PairAccept& accept);
PairAccept parse_pair_accept_frame(const MessageFrame& frame);

MessageFrame make_pair_reject_frame(const std::string& reason);
std::string parse_pair_reject_frame(const MessageFrame& frame);

MessageFrame make_pair_finalize_frame(const PairFinalize& finalize);
PairFinalize parse_pair_finalize_frame(const MessageFrame& frame);

MessageFrame make_file_chunk_frame(const void* data, std::size_t size);
MessageFrame make_transfer_complete_frame();
MessageFrame make_error_frame(const std::string& message);

std::string parse_error_frame(const MessageFrame& frame);

}  // namespace lan_transfer
