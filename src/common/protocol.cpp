#include "common/protocol.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>

#include "net/socket.h"

namespace lan_transfer {

namespace {

constexpr std::uint32_t kMaxFileNameLength = 4096;
constexpr std::uint32_t kMaxDeviceIdLength = 128;

void append_u32_be(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    buffer.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void append_u64_be(std::vector<std::uint8_t>& buffer, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        buffer.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

std::uint32_t read_u32_be(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

std::uint64_t read_u64_be(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8U) | static_cast<std::uint64_t>(data[i]);
    }
    return value;
}

void append_string(std::vector<std::uint8_t>& buffer, const std::string& value, std::uint32_t max_length) {
    if (value.empty()) {
        throw ProtocolError("string field cannot be empty");
    }
    if (value.size() > max_length) {
        throw ProtocolError("string field exceeds maximum length");
    }

    append_u32_be(buffer, static_cast<std::uint32_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

std::string read_string(const std::vector<std::uint8_t>& payload,
                        std::size_t& offset,
                        std::uint32_t max_length,
                        const char* field_name) {
    if (offset + 4 > payload.size()) {
        throw ProtocolError(std::string(field_name) + " length is missing");
    }

    const std::uint32_t length = read_u32_be(payload.data() + offset);
    offset += 4;
    if (length == 0 || length > max_length) {
        throw ProtocolError(std::string(field_name) + " length is invalid");
    }
    if (offset + length > payload.size()) {
        throw ProtocolError(std::string(field_name) + " bytes are truncated");
    }

    std::string value(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                      payload.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
    return value;
}

std::uint8_t to_wire_type(MessageType type) {
    return static_cast<std::uint8_t>(type);
}

MessageType from_wire_type(std::uint8_t value) {
    switch (value) {
    case static_cast<std::uint8_t>(MessageType::PairRequest):
        return MessageType::PairRequest;
    case static_cast<std::uint8_t>(MessageType::PairAccept):
        return MessageType::PairAccept;
    case static_cast<std::uint8_t>(MessageType::PairReject):
        return MessageType::PairReject;
    case static_cast<std::uint8_t>(MessageType::PairFinalize):
        return MessageType::PairFinalize;
    case static_cast<std::uint8_t>(MessageType::FileRequest):
        return MessageType::FileRequest;
    case static_cast<std::uint8_t>(MessageType::FileAccept):
        return MessageType::FileAccept;
    case static_cast<std::uint8_t>(MessageType::FileReject):
        return MessageType::FileReject;
    case static_cast<std::uint8_t>(MessageType::FileChunk):
        return MessageType::FileChunk;
    case static_cast<std::uint8_t>(MessageType::TransferComplete):
        return MessageType::TransferComplete;
    case static_cast<std::uint8_t>(MessageType::Error):
        return MessageType::Error;
    default:
        throw ProtocolError("received unknown message type");
    }
}

}  // namespace

ProtocolError::ProtocolError(const std::string& message) : std::runtime_error(message) {}

void ProtocolCodec::send_frame(Socket& socket, const MessageFrame& frame) const {
    if (frame.version != kVersion) {
        throw ProtocolError("unsupported outgoing protocol version");
    }
    if (frame.payload.size() > kMaxPayloadLength) {
        throw ProtocolError("payload exceeds maximum frame size");
    }

    std::array<std::uint8_t, 10> header{};
    header[0] = static_cast<std::uint8_t>((kMagic >> 24U) & 0xFFU);
    header[1] = static_cast<std::uint8_t>((kMagic >> 16U) & 0xFFU);
    header[2] = static_cast<std::uint8_t>((kMagic >> 8U) & 0xFFU);
    header[3] = static_cast<std::uint8_t>(kMagic & 0xFFU);
    header[4] = frame.version;
    header[5] = to_wire_type(frame.type);

    const std::uint32_t payload_length = static_cast<std::uint32_t>(frame.payload.size());
    header[6] = static_cast<std::uint8_t>((payload_length >> 24U) & 0xFFU);
    header[7] = static_cast<std::uint8_t>((payload_length >> 16U) & 0xFFU);
    header[8] = static_cast<std::uint8_t>((payload_length >> 8U) & 0xFFU);
    header[9] = static_cast<std::uint8_t>(payload_length & 0xFFU);

    socket.send_all(header.data(), header.size());
    if (!frame.payload.empty()) {
        socket.send_all(frame.payload.data(), frame.payload.size());
    }
}

MessageFrame ProtocolCodec::receive_frame(Socket& socket) const {
    std::array<std::uint8_t, 10> header{};
    socket.receive_exact(header.data(), header.size());

    const std::uint32_t magic = read_u32_be(header.data());
    if (magic != kMagic) {
        throw ProtocolError("received invalid frame magic");
    }

    const std::uint8_t version = header[4];
    if (version != kVersion) {
        throw ProtocolError("received unsupported protocol version");
    }

    const MessageType type = from_wire_type(header[5]);
    const std::uint32_t payload_length = read_u32_be(header.data() + 6);
    if (payload_length > kMaxPayloadLength) {
        throw ProtocolError("received payload larger than allowed");
    }

    MessageFrame frame;
    frame.version = version;
    frame.type = type;
    frame.payload.resize(payload_length);
    if (payload_length > 0) {
        socket.receive_exact(frame.payload.data(), frame.payload.size());
    }
    return frame;
}

MessageFrame make_file_request_frame(const FileRequest& request) {
    if (request.file_size > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw ProtocolError("file size is too large");
    }

    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::FileRequest;
    frame.payload.reserve(12 + request.sender_device_id.size() + request.recipient_device_id.size() +
                          request.file_name.size() + 8);

    append_string(frame.payload, request.sender_device_id, kMaxDeviceIdLength);
    append_string(frame.payload, request.recipient_device_id, kMaxDeviceIdLength);
    append_string(frame.payload, request.file_name, kMaxFileNameLength);
    append_u64_be(frame.payload, request.file_size);
    return frame;
}

FileRequest parse_file_request_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::FileRequest) {
        throw ProtocolError("expected file request frame");
    }
    if (frame.payload.size() < 20) {
        throw ProtocolError("file request payload is too short");
    }

    FileRequest request;
    std::size_t offset = 0;
    request.sender_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "sender_device_id");
    request.recipient_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "recipient_device_id");
    request.file_name = read_string(frame.payload, offset, kMaxFileNameLength, "file_name");
    if (offset + 8 != frame.payload.size()) {
        throw ProtocolError("file request payload has invalid trailing bytes");
    }
    request.file_size = read_u64_be(frame.payload.data() + offset);
    return request;
}

MessageFrame make_file_accept_frame(const FileAccept& accept) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::FileAccept;
    append_string(frame.payload, accept.recipient_device_id, kMaxDeviceIdLength);
    return frame;
}

FileAccept parse_file_accept_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::FileAccept) {
        throw ProtocolError("expected file accept frame");
    }

    FileAccept accept;
    std::size_t offset = 0;
    accept.recipient_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "recipient_device_id");
    if (offset != frame.payload.size()) {
        throw ProtocolError("file accept payload has invalid trailing bytes");
    }
    return accept;
}

MessageFrame make_file_reject_frame(const std::string& reason) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::FileReject;
    append_string(frame.payload, reason, ProtocolCodec::kMaxPayloadLength - 4U);
    return frame;
}

std::string parse_file_reject_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::FileReject) {
        throw ProtocolError("expected file reject frame");
    }

    std::size_t offset = 0;
    const std::string reason = read_string(frame.payload, offset, ProtocolCodec::kMaxPayloadLength - 4U, "reason");
    if (offset != frame.payload.size()) {
        throw ProtocolError("file reject payload has invalid trailing bytes");
    }
    return reason;
}

MessageFrame make_pair_request_frame(const PairRequest& request) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::PairRequest;
    append_string(frame.payload, request.requester_device_id, kMaxDeviceIdLength);
    return frame;
}

PairRequest parse_pair_request_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::PairRequest) {
        throw ProtocolError("expected pair request frame");
    }

    PairRequest request;
    std::size_t offset = 0;
    request.requester_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "requester_device_id");
    if (offset != frame.payload.size()) {
        throw ProtocolError("pair request payload has invalid trailing bytes");
    }
    return request;
}

MessageFrame make_pair_accept_frame(const PairAccept& accept) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::PairAccept;
    append_string(frame.payload, accept.accepter_device_id, kMaxDeviceIdLength);
    return frame;
}

PairAccept parse_pair_accept_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::PairAccept) {
        throw ProtocolError("expected pair accept frame");
    }

    PairAccept accept;
    std::size_t offset = 0;
    accept.accepter_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "accepter_device_id");
    if (offset != frame.payload.size()) {
        throw ProtocolError("pair accept payload has invalid trailing bytes");
    }
    return accept;
}

MessageFrame make_pair_reject_frame(const std::string& reason) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::PairReject;
    append_string(frame.payload, reason, ProtocolCodec::kMaxPayloadLength - 4U);
    return frame;
}

std::string parse_pair_reject_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::PairReject) {
        throw ProtocolError("expected pair reject frame");
    }

    std::size_t offset = 0;
    const std::string reason = read_string(frame.payload, offset, ProtocolCodec::kMaxPayloadLength - 4U, "reason");
    if (offset != frame.payload.size()) {
        throw ProtocolError("pair reject payload has invalid trailing bytes");
    }
    return reason;
}

MessageFrame make_pair_finalize_frame(const PairFinalize& finalize) {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::PairFinalize;
    append_string(frame.payload, finalize.requester_device_id, kMaxDeviceIdLength);
    append_string(frame.payload, finalize.accepter_device_id, kMaxDeviceIdLength);
    return frame;
}

PairFinalize parse_pair_finalize_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::PairFinalize) {
        throw ProtocolError("expected pair finalize frame");
    }

    PairFinalize finalize;
    std::size_t offset = 0;
    finalize.requester_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "requester_device_id");
    finalize.accepter_device_id = read_string(frame.payload, offset, kMaxDeviceIdLength, "accepter_device_id");
    if (offset != frame.payload.size()) {
        throw ProtocolError("pair finalize payload has invalid trailing bytes");
    }
    return finalize;
}

MessageFrame make_file_chunk_frame(const void* data, std::size_t size) {
    if (size > ProtocolCodec::kMaxPayloadLength) {
        throw ProtocolError("file chunk exceeds maximum frame size");
    }

    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::FileChunk;
    frame.payload.resize(size);
    if (size > 0) {
        std::memcpy(frame.payload.data(), data, size);
    }
    return frame;
}

MessageFrame make_transfer_complete_frame() {
    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::TransferComplete;
    return frame;
}

MessageFrame make_error_frame(const std::string& message) {
    if (message.size() > ProtocolCodec::kMaxPayloadLength) {
        throw ProtocolError("error message exceeds maximum frame size");
    }

    MessageFrame frame;
    frame.version = ProtocolCodec::kVersion;
    frame.type = MessageType::Error;
    frame.payload.assign(message.begin(), message.end());
    return frame;
}

std::string parse_error_frame(const MessageFrame& frame) {
    if (frame.type != MessageType::Error) {
        throw ProtocolError("expected error frame");
    }
    return std::string(frame.payload.begin(), frame.payload.end());
}

}  // namespace lan_transfer
