//
// Created by vladim0105 on 29.06.2021.
//

#ifndef DOMOS_TWAMP_LIGHT_TWAMP_LIGHT_HPP
#define DOMOS_TWAMP_LIGHT_TWAMP_LIGHT_HPP
#include "packets.h"
#include <cstdint>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <vector>

constexpr int HDR_TTL = 255; /* TTL=255 in TWAMP for IP Header */
constexpr int64_t NSEC_PER_SEC = 1000000000LL;

/**
 * @brief Safely add two timespec values with overflow protection.
 *
 * Unlike the tspecplus macro from qoo-c, this function uses int64_t
 * for intermediate calculations to prevent integer overflow when
 * adding tv_nsec values.
 */
inline void safe_tspecplus(const timespec *a, const timespec *b, timespec *result)
{
    result->tv_sec = a->tv_sec + b->tv_sec;
    // Use int64_t to prevent overflow during addition
    int64_t nsec = static_cast<int64_t>(a->tv_nsec) + static_cast<int64_t>(b->tv_nsec);
    while (nsec >= NSEC_PER_SEC) {
        ++result->tv_sec;
        nsec -= NSEC_PER_SEC;
    }
    result->tv_nsec = static_cast<long>(nsec);
}
constexpr int SERVER_PORT = 862;
constexpr int CHECK_TIMES = 100;
constexpr int IPV4 = 4;
constexpr int IPV6 = 6;

/* TWAMP timestamp is NTP time (RFC1305).
 * Should be in network byte order!      */
using IPHeader = struct ip_header {
    uint8_t ttl;
    uint8_t tos;
};

void timeval_to_timestamp(const struct timeval *tv, Timestamp *ts);
void timespec_to_timestamp(const struct timespec *tv, Timestamp *ts);

void timestamp_to_timeval(const Timestamp *ts, struct timeval *tv);

auto timestamp_to_usec(const Timestamp *ts) -> uint64_t;
auto timestamp_to_nsec(const Timestamp *ts) -> uint64_t;
auto nanosecondsToTimespec(uint64_t delay_epoch_nanoseconds) -> struct timespec;
auto get_usec() -> uint64_t;
auto get_timestamp() -> Timestamp;

auto get_ip_header(msghdr hdr) -> IPHeader;
void set_socket_options(int socket, uint8_t ip_ttl, uint8_t timeout_secs);
void set_socket_tos(int socket, uint8_t ip_tos);
void get_kernel_timestamp(struct msghdr incoming_msg, struct timespec *incoming_timestamp);
auto isWithinEpsilon(double a, double b, double percentEpsilon) -> bool;
template <class T> auto vectorToString(std::vector<T> vec, const std::string &sep) -> std::string
{
    std::stringstream result;
    std::copy(vec.begin(), vec.end(), std::ostream_iterator<T>(result, sep.c_str()));
    return result.str().substr(0, result.str().size() - 1);
}
template <typename Iter, typename RandomGenerator>
auto select_randomly(Iter start, Iter end, RandomGenerator &g) -> Iter
{
    auto dist = std::distance(start, end);
    std::uniform_int_distribution<> dis(0, static_cast<int>(dist) - 1);
    std::advance(start, dis(g));
    return start;
}

template <typename Iter> auto select_randomly(Iter start, Iter end, uint32_t seed = 0) -> Iter
{
    static std::random_device rd;
    static std::mt19937 gen(seed == 0 ? rd() : seed);
    return select_randomly(start, end, gen);
}
auto ntohts(Timestamp ts) -> Timestamp;
auto htonts(Timestamp ts) -> Timestamp;
auto parseIPPort(const std::string &input, std::string &ip, uint16_t &port) -> bool;
auto parseIPv6Port(const std::string &input, std::string &ip, uint16_t &port) -> bool;
auto make_msghdr(
    struct iovec *iov, size_t iov_len, struct sockaddr_in6 *addr, socklen_t addr_len, char *control, size_t control_len)
    -> struct msghdr;
void parse_ip_address(struct msghdr sender_msg, uint16_t *port, char *host, uint8_t ip_version);
#endif // DOMOS_TWAMP_LIGHT_TWAMP_LIGHT_HPP
