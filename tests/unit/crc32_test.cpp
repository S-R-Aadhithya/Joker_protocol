#include <gtest/gtest.h>
#include "joker/crc32.hpp"
#include <string>

using namespace joker;

TEST(Crc32Test, IsoHdlcVectors) {
    // Test vectors for CRC-32/ISO-HDLC
    std::string empty = "";
    EXPECT_EQ(crc32_iso_hdlc(reinterpret_cast<const uint8_t*>(empty.data()), empty.size()), 0x00000000);

    std::string test1 = "123456789";
    EXPECT_EQ(crc32_iso_hdlc(reinterpret_cast<const uint8_t*>(test1.data()), test1.size()), 0xCBF43926);

    std::string test2 = "The quick brown fox jumps over the lazy dog";
    EXPECT_EQ(crc32_iso_hdlc(reinterpret_cast<const uint8_t*>(test2.data()), test2.size()), 0x414FA339);
}

TEST(Crc32Test, JokerPacketId) {
    std::string test1 = "123456789";
    EXPECT_EQ(joker_packet_id(reinterpret_cast<const uint8_t*>(test1.data()), test1.size()), 0xCBF43926);
}
