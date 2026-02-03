/**
 * Unit tests for packet structures (packets.h)
 */

#include <gtest/gtest.h>
#include "packets.h"
#include <cstring>

// ============================================================================
// Tests for Timestamp structure
// ============================================================================

class TimestampStructTest : public ::testing::Test {};

TEST_F(TimestampStructTest, DefaultInitialization) {
    Timestamp ts{};
    EXPECT_EQ(ts.integer, 0U);
    EXPECT_EQ(ts.fractional, 0U);
}

TEST_F(TimestampStructTest, SizeIs8Bytes) {
    // Timestamp should be exactly 8 bytes (2 x uint32_t)
    EXPECT_EQ(sizeof(Timestamp), 8U);
}

TEST_F(TimestampStructTest, FieldAssignment) {
    Timestamp ts;
    ts.integer = 0x12345678;
    ts.fractional = 0xABCDEF01;
    
    EXPECT_EQ(ts.integer, 0x12345678U);
    EXPECT_EQ(ts.fractional, 0xABCDEF01U);
}

// ============================================================================
// Tests for ClientPacket structure
// ============================================================================

class ClientPacketTest : public ::testing::Test {};

TEST_F(ClientPacketTest, DefaultInitialization) {
    ClientPacket packet{};
    EXPECT_EQ(packet.seq_number, 0U);
    EXPECT_EQ(packet.timestamp.integer, 0U);
    EXPECT_EQ(packet.timestamp.fractional, 0U);
    EXPECT_EQ(packet.timestamp_error_estimate, 0U);
}

TEST_F(ClientPacketTest, SizeMatchesTST_PKT_SIZE) {
    // ClientPacket should be TST_PKT_SIZE bytes
    EXPECT_EQ(sizeof(ClientPacket), TST_PKT_SIZE);
}

TEST_F(ClientPacketTest, PaddingArraySize) {
    ClientPacket packet{};
    // Padding size = TST_PKT_SIZE - 14 (seq_number:4 + timestamp:8 + error_estimate:2)
    EXPECT_EQ(packet.padding.size(), static_cast<size_t>(TST_PKT_SIZE - 14));
}

TEST_F(ClientPacketTest, FieldAssignment) {
    ClientPacket packet;
    packet.seq_number = 42;
    packet.timestamp.integer = 1000;
    packet.timestamp.fractional = 2000;
    packet.timestamp_error_estimate = 0x8001;
    
    EXPECT_EQ(packet.seq_number, 42U);
    EXPECT_EQ(packet.timestamp.integer, 1000U);
    EXPECT_EQ(packet.timestamp.fractional, 2000U);
    EXPECT_EQ(packet.timestamp_error_estimate, 0x8001U);
}

TEST_F(ClientPacketTest, PaddingIsZeroInitialized) {
    ClientPacket packet{};
    
    // All padding bytes should be zero
    for (size_t i = 0; i < packet.padding.size(); i++) {
        EXPECT_EQ(packet.padding[i], 0U) << "Padding byte " << i << " is not zero";
    }
}

// ============================================================================
// Tests for ReflectorPacket structure
// ============================================================================

class ReflectorPacketTest : public ::testing::Test {};

TEST_F(ReflectorPacketTest, DefaultInitialization) {
    ReflectorPacket packet{};
    EXPECT_EQ(packet.seq_number, 0U);
    EXPECT_EQ(packet.timestamp.integer, 0U);
    EXPECT_EQ(packet.timestamp.fractional, 0U);
    EXPECT_EQ(packet.timestamp_error_estimate, 0U);
    EXPECT_EQ(packet.receive_timestamp.integer, 0U);
    EXPECT_EQ(packet.receive_timestamp.fractional, 0U);
    EXPECT_EQ(packet.sender_seq_number, 0U);
    EXPECT_EQ(packet.sender_timestamp.integer, 0U);
    EXPECT_EQ(packet.sender_timestamp.fractional, 0U);
    EXPECT_EQ(packet.sender_error_estimate, 0U);
    EXPECT_EQ(packet.sender_ttl, 0U);
    EXPECT_EQ(packet.sender_tos, 0U);
}

TEST_F(ReflectorPacketTest, SizeMatchesTST_PKT_SIZE) {
    // ReflectorPacket should also be TST_PKT_SIZE bytes
    EXPECT_EQ(sizeof(ReflectorPacket), TST_PKT_SIZE);
}

TEST_F(ReflectorPacketTest, HeaderSizeConstant) {
    // Verify REFLECTOR_HEADER_SIZE matches actual header size
    // Header: seq_number(4) + timestamp(8) + error_estimate(2) + mbz1(2) + 
    //         receive_timestamp(8) + sender_seq_number(4) + sender_timestamp(8) +
    //         sender_error_estimate(2) + mbz2(2) + sender_ttl(1) + sender_tos(1)
    // Total = 4 + 8 + 2 + 2 + 8 + 4 + 8 + 2 + 2 + 1 + 1 = 42
    EXPECT_EQ(REFLECTOR_HEADER_SIZE, 42);
}

TEST_F(ReflectorPacketTest, PaddingArraySize) {
    ReflectorPacket packet{};
    EXPECT_EQ(packet.padding.size(), static_cast<size_t>(TST_PKT_SIZE - REFLECTOR_HEADER_SIZE));
}

TEST_F(ReflectorPacketTest, MBZFieldsAreZero) {
    ReflectorPacket packet{};
    
    // MBZ (Must Be Zero) fields should be zero
    for (auto byte : packet.mbz1) {
        EXPECT_EQ(byte, 0U);
    }
    for (auto byte : packet.mbz2) {
        EXPECT_EQ(byte, 0U);
    }
}

TEST_F(ReflectorPacketTest, FieldAssignment) {
    ReflectorPacket packet;
    packet.seq_number = 100;
    packet.sender_seq_number = 100;
    packet.sender_ttl = 64;
    packet.sender_tos = 0x20;
    
    EXPECT_EQ(packet.seq_number, 100U);
    EXPECT_EQ(packet.sender_seq_number, 100U);
    EXPECT_EQ(packet.sender_ttl, 64U);
    EXPECT_EQ(packet.sender_tos, 0x20U);
}

// ============================================================================
// Tests for packet size constants
// ============================================================================

TEST(PacketConstantsTest, TST_PKT_SIZE_Value) {
    // MTU 1514 - 20 (IP header) - 8 (UDP header) = 1472 + padding consideration
    EXPECT_EQ(TST_PKT_SIZE, 1472);
}

TEST(PacketConstantsTest, REFLECTOR_HEADER_SIZE_Value) {
    EXPECT_EQ(REFLECTOR_HEADER_SIZE, 42);
}

// ============================================================================
// Tests for minimum payload sizes
// ============================================================================

TEST(PayloadSizeTest, MinimumClientPayload) {
    // Minimum payload should include at least the header fields
    // seq_number(4) + timestamp(8) + error_estimate(2) = 14 bytes
    size_t min_header = sizeof(uint32_t) + sizeof(Timestamp) + sizeof(uint16_t);
    EXPECT_EQ(min_header, 14U);
}

TEST(PayloadSizeTest, MinimumReflectorPayload) {
    // Minimum reflector payload is the header size
    EXPECT_EQ(REFLECTOR_HEADER_SIZE, 42);
}

// ============================================================================
// Tests for memory layout (ensure no unexpected padding)
// ============================================================================

TEST(MemoryLayoutTest, ClientPacketContiguous) {
    ClientPacket packet;
    
    // Verify the packet can be treated as contiguous memory
    char* base = reinterpret_cast<char*>(&packet);
    char* seq_ptr = reinterpret_cast<char*>(&packet.seq_number);
    char* ts_ptr = reinterpret_cast<char*>(&packet.timestamp);
    char* err_ptr = reinterpret_cast<char*>(&packet.timestamp_error_estimate);
    char* pad_ptr = reinterpret_cast<char*>(&packet.padding);
    
    // Check field offsets are as expected
    EXPECT_EQ(seq_ptr - base, 0);
    EXPECT_EQ(ts_ptr - base, 4);  // After seq_number (4 bytes)
    EXPECT_EQ(err_ptr - base, 12); // After timestamp (4 + 8 bytes)
    EXPECT_EQ(pad_ptr - base, 14); // After error_estimate (4 + 8 + 2 bytes)
}

TEST(MemoryLayoutTest, ReflectorPacketContiguous) {
    ReflectorPacket packet;
    
    char* base = reinterpret_cast<char*>(&packet);
    char* seq_ptr = reinterpret_cast<char*>(&packet.seq_number);
    char* ts_ptr = reinterpret_cast<char*>(&packet.timestamp);
    
    EXPECT_EQ(seq_ptr - base, 0);
    EXPECT_EQ(ts_ptr - base, 4);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
