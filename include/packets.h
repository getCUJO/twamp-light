//
// Created by vladim0105 on 12/17/21.
//

#ifndef TWAMP_LIGHT_PACKETS_H
#define TWAMP_LIGHT_PACKETS_H

#include <array>
#include <cstdint>

#define TST_PKT_SIZE 1472        // 1472 (MTU 1514)
#define REFLECTOR_HEADER_SIZE 42 // Size of the ReflectorPacket header
struct Timestamp {
    uint32_t integer = 0;
    uint32_t fractional = 0;
};

/* Session-Sender TWAMP-Test packet for Unauthenticated mode */
struct ClientPacket {
    uint32_t seq_number = 0;
    Timestamp timestamp = {};
    uint16_t timestamp_error_estimate = 0;
    std::array<uint8_t, TST_PKT_SIZE - 14> padding{};
};

/* Session-Reflector TWAMP-Test packet for Unauthenticated mode */
struct ReflectorPacket {
    uint32_t seq_number = 0;
    Timestamp timestamp = {};
    uint16_t timestamp_error_estimate = 0;
    std::array<uint8_t, 2> mbz1{};
    Timestamp receive_timestamp = {};
    uint32_t sender_seq_number = 0;
    Timestamp sender_timestamp = {};
    uint16_t sender_error_estimate = 0;
    std::array<uint8_t, 2> mbz2{};
    uint8_t sender_ttl = 0;
    uint8_t sender_tos = 0;
    std::array<uint8_t, TST_PKT_SIZE - REFLECTOR_HEADER_SIZE> padding{};
};
#endif // TWAMP_LIGHT_PACKETS_H
