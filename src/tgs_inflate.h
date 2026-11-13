// Internal: self-contained DEFLATE decoder (RFC 1951) + gzip (RFC 1952) / zlib (RFC 1950) wrappers.
// No zlib/miniz dependency -> no symbol clashes inside your DLL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace tgs {
namespace detail {

enum class InflateStatus
{
    Ok,
    BadData,     // corrupted stream
    Truncated,   // input ended early
    TooLarge,    // output would exceed max_output
};

// Decodes a raw DEFLATE stream from src, appending to out.
// *consumed receives the number of input bytes used (rounded up to a whole byte).
InflateStatus InflateRaw(const uint8_t* src, size_t src_len, std::string& out, size_t max_output,
                         size_t* consumed, const char** error_text);

uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0);
uint32_t Adler32(const uint8_t* data, size_t len, uint32_t adler = 1);

} // namespace detail
} // namespace tgs
