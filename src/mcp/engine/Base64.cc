#include "mcp/engine/Base64.h"

#include <string>

namespace mcp {

std::string base64Encode(const std::string& bytes)
{
  static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  size_t i = 0;
  while (i + 2 < bytes.size()) {
    const unsigned a = static_cast<unsigned char>(bytes[i]);
    const unsigned b = static_cast<unsigned char>(bytes[i + 1]);
    const unsigned c = static_cast<unsigned char>(bytes[i + 2]);
    out.push_back(alphabet[a >> 2]);
    out.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
    out.push_back(alphabet[((b & 15) << 2) | (c >> 6)]);
    out.push_back(alphabet[c & 63]);
    i += 3;
  }
  if (i < bytes.size()) {
    const unsigned a = static_cast<unsigned char>(bytes[i]);
    const unsigned b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
    out.push_back(alphabet[a >> 2]);
    out.push_back(alphabet[((a & 3) << 4) | (b >> 4)]);
    out.push_back(i + 1 < bytes.size() ? alphabet[(b & 15) << 2] : '=');
    out.push_back('=');
  }
  return out;
}

}  // namespace mcp
