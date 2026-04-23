#include "app/file_sender.h"

#include <array>
#include <fstream>
#include <stdexcept>

#include "common/protocol.h"
#include "common/types.h"
#include "net/socket.h"

namespace lan_transfer {

FileSender::FileSender(PairStore& pair_store, const AppSettings& settings)
    : pair_store_(pair_store), max_file_size_bytes_(settings.max_file_size_bytes) {}

void FileSender::send_file(const std::string& host,
                           std::uint16_t port,
                           const std::string& target_device_id,
                           const std::filesystem::path& file_path) const {
    if (!std::filesystem::exists(file_path)) {
        throw std::runtime_error("file does not exist: " + file_path.string());
    }
    if (!std::filesystem::is_regular_file(file_path)) {
        throw std::runtime_error("path is not a regular file: " + file_path.string());
    }
    if (!pair_store_.is_device_paired(target_device_id)) {
        throw std::runtime_error("target device is not paired: " + target_device_id);
    }
    const auto paired_device = pair_store_.find_paired_device(target_device_id);
    if (!paired_device.has_value()) {
        throw std::runtime_error("target device pairing record is missing");
    }
    if (!paired_device->host.empty() && paired_device->host != host) {
        throw std::runtime_error("target host does not match paired device record");
    }
    if (paired_device->port != 0 && paired_device->port != port) {
        throw std::runtime_error("target port does not match paired device record");
    }

    const std::uint64_t file_size = std::filesystem::file_size(file_path);
    if (file_size == 0) {
        throw std::runtime_error("selected file is empty");
    }
    if (file_size > max_file_size_bytes_) {
        throw std::runtime_error("selected file exceeds the configured maximum file size");
    }

    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open file for reading: " + file_path.string());
    }
    const std::string local_device_id = pair_store_.get_or_create_local_device_id();

    Socket socket;
    socket.connect_to(host, port);
    ProtocolCodec codec;

    FileRequest request{local_device_id, target_device_id, file_path.filename().string(), file_size};
    codec.send_frame(socket, make_file_request_frame(request));

    const MessageFrame response_frame = codec.receive_frame(socket);
    if (response_frame.type == MessageType::FileReject) {
        throw std::runtime_error("receiver rejected file: " + parse_file_reject_frame(response_frame));
    }
    if (response_frame.type == MessageType::Error) {
        throw std::runtime_error("peer reported error: " + parse_error_frame(response_frame));
    }
    if (response_frame.type != MessageType::FileAccept) {
        throw std::runtime_error("expected file accept or file reject response");
    }

    const FileAccept accept = parse_file_accept_frame(response_frame);
    if (accept.recipient_device_id != target_device_id) {
        throw std::runtime_error("file accept response does not match target device");
    }

    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = input.gcount();
        if (bytes_read > 0) {
            codec.send_frame(socket, make_file_chunk_frame(buffer.data(), static_cast<std::size_t>(bytes_read)));
        }
    }

    if (!input.eof()) {
        throw std::runtime_error("failed while reading file: " + file_path.string());
    }

    codec.send_frame(socket, make_transfer_complete_frame());
}

}  // namespace lan_transfer
