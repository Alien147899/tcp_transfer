#include "app/file_receiver.h"

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "common/protocol.h"
#include "common/types.h"
#include "discovery/code_lookup_service.h"
#include "discovery/presence_announcer.h"
#include "net/socket.h"
#include "pairing/pairing_manager.h"

namespace lan_transfer {

FileReceiver::FileReceiver(PairStore& pair_store, SecurityManager& security_manager)
    : pair_store_(pair_store), security_manager_(security_manager) {}

namespace {

bool prompt_for_file_confirmation(const FileRequest& request) {
    std::cout << "Incoming file request" << '\n';
    std::cout << "Sender: " << request.sender_device_id << '\n';
    std::cout << "File name: " << request.file_name << '\n';
    std::cout << "File size: " << request.file_size << " bytes" << '\n';
    std::cout << "Accept file transfer? [y/N]:" << '\n' << std::flush;

    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES";
}

}  // namespace

void FileReceiver::run_server(std::uint16_t port, const std::filesystem::path& output_directory) const {
    std::filesystem::create_directories(output_directory);
    pair_store_.get_or_create_local_device_id();

    Socket server_socket;
    server_socket.bind_and_listen(port);
    PresenceAnnouncer announcer(pair_store_);
    announcer.start(port);
    CodeLookupService code_lookup_service(pair_store_);
    code_lookup_service.start(port);

    std::cout << "Listening on port " << port << '\n';
    while (true) {
        Socket client_socket = server_socket.accept_connection();
        ProtocolCodec codec;
        std::filesystem::path temp_path_to_cleanup;
        try {
            const std::string peer_host = client_socket.get_peer_ip();
            const MessageFrame initial_frame = codec.receive_frame(client_socket);
            if (initial_frame.type == MessageType::PairRequest) {
                const PairRequest request = parse_pair_request_frame(initial_frame);
                security_manager_.enforce_pair_request_allowed(request, peer_host);

                PairingManager pairing_manager(pair_store_);
                pairing_manager.handle_incoming_pair_request(client_socket, codec, initial_frame);
                continue;
            }
            if (initial_frame.type == MessageType::Error) {
                throw ProtocolError("peer reported error: " + parse_error_frame(initial_frame));
            }
            if (initial_frame.type != MessageType::FileRequest) {
                throw ProtocolError("expected pair request or file request frame");
            }

            const FileRequest request = parse_file_request_frame(initial_frame);
            security_manager_.enforce_file_request_allowed(request, peer_host);

            const std::string local_device_id = pair_store_.get_or_create_local_device_id();
            if (request.recipient_device_id != local_device_id) {
                throw ProtocolError("file recipient device_id does not match local device");
            }
            if (!pair_store_.is_device_paired(request.sender_device_id)) {
                throw ProtocolError("sender device is not paired");
            }
            if (!prompt_for_file_confirmation(request)) {
                codec.send_frame(client_socket, make_file_reject_frame("file transfer rejected by receiver"));
                std::cout << "File transfer rejected." << '\n';
                continue;
            }

            codec.send_frame(client_socket, make_file_accept_frame(FileAccept{local_device_id}));
            std::cout << "Client connected, receiving file..." << '\n';

            const std::filesystem::path output_path =
                security_manager_.path_sanitizer().build_output_path(request.file_name);
            const std::filesystem::path temp_path =
                security_manager_.path_sanitizer().build_temp_path(request.file_name);
            temp_path_to_cleanup = temp_path;
            std::filesystem::remove(temp_path);

            std::ofstream output(temp_path, std::ios::binary);
            if (!output) {
                throw std::runtime_error("failed to open temporary output file: " + temp_path.string());
            }

            std::uint64_t remaining = request.file_size;
            while (remaining > 0) {
                const MessageFrame frame = codec.receive_frame(client_socket);
                if (frame.type == MessageType::Error) {
                    throw ProtocolError("peer reported error: " + parse_error_frame(frame));
                }
                if (frame.type != MessageType::FileChunk) {
                    throw ProtocolError("expected file chunk frame while receiving file data");
                }
                if (frame.payload.size() > remaining) {
                    throw ProtocolError("received more file data than declared in metadata");
                }

                output.write(reinterpret_cast<const char*>(frame.payload.data()),
                             static_cast<std::streamsize>(frame.payload.size()));
                if (!output) {
                    throw std::runtime_error("failed while writing temporary output file: " + temp_path.string());
                }
                remaining -= static_cast<std::uint64_t>(frame.payload.size());
            }

            const MessageFrame completion_frame = codec.receive_frame(client_socket);
            if (completion_frame.type == MessageType::Error) {
                throw ProtocolError("peer reported error: " + parse_error_frame(completion_frame));
            }
            if (completion_frame.type != MessageType::TransferComplete) {
                throw ProtocolError("expected transfer complete frame after file data");
            }

            output.close();
            if (std::filesystem::exists(output_path)) {
                std::filesystem::remove(output_path);
            }
            std::filesystem::rename(temp_path, output_path);
            temp_path_to_cleanup.clear();
            std::cout << "Saved file to: " << output_path.string() << '\n';
        } catch (...) {
            if (!temp_path_to_cleanup.empty()) {
                std::filesystem::remove(temp_path_to_cleanup);
            }
            try {
                codec.send_frame(client_socket, make_error_frame("request rejected or failed"));
            } catch (...) {
            }
            try {
                throw;
            } catch (const std::exception& ex) {
                std::cerr << "Request failed: " << ex.what() << '\n';
            }
        }
    }
}

}  // namespace lan_transfer
