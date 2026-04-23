#include "discovery/connection_code.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace lan_transfer {

std::string make_connection_code(const std::string& device_id) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char ch : device_id) {
        hash ^= ch;
        hash *= 16777619u;
    }

    const std::uint32_t code_value = 100000u + (hash % 900000u);
    std::ostringstream output;
    output << std::setw(6) << std::setfill('0') << code_value;
    return output.str();
}

}  // namespace lan_transfer
