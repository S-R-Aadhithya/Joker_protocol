#pragma once
#include <cstdint>
#include <cstddef>

namespace joker {

// CRC-32/ISO-HDLC (polynomial 0xEDB88320, reflected, init 0xFFFFFFFF,
// final XOR 0xFFFFFFFF) — the "zlib"/Ethernet variant.
// [ENGINEERING] choice — see gap note above; NOT explicitly mandated by the
// paper's polynomial/variant (the paper does not specify one).
uint32_t crc32_iso_hdlc(const uint8_t* data, size_t length);

// joker_packet_id() is what candidate.cpp / forwarding.cpp actually call;
// it exists as a named seam so the CRC variant can be swapped in one place
// if interop testing against another implementation reveals a mismatch.
inline uint32_t joker_packet_id(const uint8_t* payload, size_t length) {
    return crc32_iso_hdlc(payload, length);
}

}  // namespace joker
