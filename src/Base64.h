#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vwave {

std::string base64Encode(const uint8_t* data, size_t size);
bool base64Decode(const std::string& text, std::vector<uint8_t>& out);

} // namespace vwave
