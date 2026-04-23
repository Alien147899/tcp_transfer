#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "app/file_receiver.h"
#include "app/file_sender.h"
#include "config/settings.h"
#include "discovery/device_discovery.h"
#include "net/socket.h"
#include "pairing/pair_store.h"
#include "pairing/pairing_manager.h"
#include "security/security_manager.h"

namespace {

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  lan_transfer receive <port> <output_directory>\n"
        << "  lan_transfer discover <listen_port> <duration_seconds>\n"
        << "  lan_transfer pair <ip> <port>\n"
        << "  lan_transfer send <ip> <port> <target_device_id> <file_path>\n";
}

std::uint16_t parse_port(const std::string& port_text) {
    const unsigned long value = std::stoul(port_text);
    if (value > 65535UL) {
        throw std::runtime_error("port must be in range 0-65535");
    }
    return static_cast<std::uint16_t>(value);
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        lan_transfer::SocketSystem socket_system;
        const std::filesystem::path storage_directory = std::filesystem::current_path();
        const lan_transfer::AppSettings settings = lan_transfer::SettingsLoader::load(storage_directory);
        lan_transfer::PairStore pair_store(storage_directory);

        if (argc < 2) {
            print_usage();
            return EXIT_FAILURE;
        }

        const std::string mode = argv[1];
        if (mode == "receive") {
            if (argc != 4) {
                print_usage();
                return EXIT_FAILURE;
            }

            const std::uint16_t port = parse_port(argv[2]);
            const std::filesystem::path output_directory = argv[3];

            lan_transfer::SecurityManager security_manager(storage_directory, output_directory, settings);
            lan_transfer::FileReceiver receiver(pair_store, security_manager);
            receiver.run_server(port, output_directory);
            return EXIT_SUCCESS;
        }

        if (mode == "discover") {
            if (argc != 4) {
                print_usage();
                return EXIT_FAILURE;
            }

            const std::uint16_t listen_port = parse_port(argv[2]);
            const int duration_value = std::stoi(argv[3]);
            if (duration_value <= 0) {
                throw std::runtime_error("duration_seconds must be positive");
            }

            lan_transfer::DeviceDiscoveryService discovery_service(pair_store);
            const auto devices =
                discovery_service.discover(listen_port, std::chrono::seconds(duration_value));

            std::cout << "Online devices:" << '\n';
            if (devices.empty()) {
                std::cout << "  (none discovered)" << '\n';
            } else {
                for (const auto& device : devices) {
                    std::cout << "  " << device.device_name
                              << " [" << device.device_id << "] "
                              << device.host << ":" << device.listen_port
                              << " status=" << (device.paired ? "paired" : "unpaired") << '\n';
                }
            }
            std::cout << "Discovery only lists devices. Unpaired devices still cannot send files." << '\n';
            return EXIT_SUCCESS;
        }

        if (mode == "pair") {
            if (argc != 4) {
                print_usage();
                return EXIT_FAILURE;
            }

            const std::string host = argv[2];
            const std::uint16_t port = parse_port(argv[3]);

            lan_transfer::PairingManager pairing_manager(pair_store);
            pairing_manager.initiate_pairing(host, port);
            std::cout << "Pairing completed successfully." << '\n';
            return EXIT_SUCCESS;
        }

        if (mode == "send") {
            if (argc != 6) {
                print_usage();
                return EXIT_FAILURE;
            }

            const std::string host = argv[2];
            const std::uint16_t port = parse_port(argv[3]);
            const std::string target_device_id = argv[4];
            const std::filesystem::path file_path = argv[5];

            lan_transfer::FileSender sender(pair_store, settings);
            sender.send_file(host, port, target_device_id, file_path);
            std::cout << "File sent successfully." << '\n';
            return EXIT_SUCCESS;
        }

        print_usage();
        return EXIT_FAILURE;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
