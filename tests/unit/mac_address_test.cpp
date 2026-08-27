#include <gtest/gtest.h>
#include "joker/mac_address.hpp"

using namespace joker;

TEST(MacAddressTest, ParseValid) {
    auto mac = MacAddress::Parse("01:23:45:67:89:ab");
    ASSERT_TRUE(mac.has_value());
    EXPECT_EQ(mac->ToString(), "01:23:45:67:89:ab");
    EXPECT_EQ(mac->bytes()[0], 0x01);
    EXPECT_EQ(mac->bytes()[5], 0xab);

    auto mac2 = MacAddress::Parse("AA-BB-CC-DD-EE-FF");
    ASSERT_TRUE(mac2.has_value());
    EXPECT_EQ(mac2->ToString(), "aa:bb:cc:dd:ee:ff");
}

TEST(MacAddressTest, ParseInvalid) {
    EXPECT_FALSE(MacAddress::Parse("01:23:45:67:89:a"));
    EXPECT_FALSE(MacAddress::Parse("01:23:45:67:89:abc"));
    EXPECT_FALSE(MacAddress::Parse("01:23:45:67:89-ab"));
    EXPECT_FALSE(MacAddress::Parse("01:23:45:67:89:zz"));
    EXPECT_FALSE(MacAddress::Parse(""));
}

TEST(MacAddressTest, Properties) {
    auto bcast = MacAddress::Parse("ff:ff:ff:ff:ff:ff");
    EXPECT_TRUE(bcast->IsBroadcast());
    EXPECT_FALSE(bcast->IsZero());

    auto zero = MacAddress::Parse("00:00:00:00:00:00");
    EXPECT_TRUE(zero->IsZero());
    EXPECT_FALSE(zero->IsBroadcast());
}
