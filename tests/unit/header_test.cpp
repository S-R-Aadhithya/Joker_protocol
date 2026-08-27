#include <gtest/gtest.h>
#include "joker/header.hpp"
#include <vector>

using namespace joker;

TEST(HeaderTest, SerializeDeserialize) {
    JokerHeader h{};
    h.type = PacketType::kUnicast;
    h.ttl = 32;
    h.packet_id = 0x12345678;
    h.final_destination = MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});
    h.other_candidates.push_back(MacAddress({0x11, 0x22, 0x33, 0x44, 0x55, 0x66}));
    h.other_candidates.push_back(MacAddress({0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc}));

    std::vector<uint8_t> buffer;
    EXPECT_TRUE(serialize_header(h, buffer));
    EXPECT_EQ(buffer.size(), h.WireSize());

    size_t consumed = 0;
    auto parsed = deserialize_header(buffer, consumed);
    
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(consumed, buffer.size());
    EXPECT_EQ(parsed->type, h.type);
    EXPECT_EQ(parsed->ttl, h.ttl);
    EXPECT_EQ(parsed->packet_id, h.packet_id);
    EXPECT_EQ(parsed->final_destination, h.final_destination);
    EXPECT_EQ(parsed->other_candidates, h.other_candidates);
}

TEST(HeaderTest, DeserializeMalformed) {
    std::vector<uint8_t> buffer = {0x00, 0x20}; // too short
    size_t consumed = 0;
    auto parsed = deserialize_header(buffer, consumed);
    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(consumed, 0);
}

TEST(HeaderTest, DeserializeInvalidType) {
    std::vector<uint8_t> buffer(20, 0); // valid size, but type is 0
    buffer[0] = 255; // Invalid type
    size_t consumed = 0;
    auto parsed = deserialize_header(buffer, consumed);
    EXPECT_FALSE(parsed.has_value());
}

TEST(HeaderTest, ValidateHeader) {
    JokerHeader h{};
    h.ttl = 32;
    h.final_destination = MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});
    h.other_candidates.push_back(MacAddress({0x11, 0x22, 0x33, 0x44, 0x55, 0x66}));
    
    // Valid
    EXPECT_TRUE(validate_header(h, 2));



    // Broadcast Dest
    h.final_destination = MacAddress({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    EXPECT_FALSE(validate_header(h, 2));
    h.final_destination = MacAddress({0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff});

    // Too many candidates
    EXPECT_FALSE(validate_header(h, 1)); // Max is 1, but we have 1 top + 1 other = 2

    // Duplicate candidates
    h.other_candidates.push_back(MacAddress({0x11, 0x22, 0x33, 0x44, 0x55, 0x66})); // duplicate
    EXPECT_FALSE(validate_header(h, 3));
}
