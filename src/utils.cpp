/*
 * Modified by Domos, original:
 *
 * Name: Emma Mirică
 * Project: TWAMP Protocol
 * Class: OSS
 * Email: emma.mirica@cti.pub.ro
 * Contributions: stephanDB
 *
 * Source: timestamp.c
 * Note: contains helpful functions to get the timestamp
 * in the required TWAMP format.
 *
 */

#include "utils.hpp"
#include <sys/time.h>
#include <time.h>
#include "cstdlib"
#include <cmath>
#include <cstdint>

// NTP epoch offset in seconds (difference between 1900 and 1970)
constexpr uint32_t NTP_EPOCH_OFFSET = 2208988800UL;
#include <arpa/inet.h>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <linux/net_tstamp.h>
#include <sys/time.h>

// Define a named constant for the default TTL value
constexpr uint8_t DEFAULT_TTL = 255;

// Constants
constexpr double MICROSECONDS_IN_SECOND = 1e6;
constexpr double NANOSECONDS_IN_SECOND = 1e9;
constexpr int64_t NANOSECONDS_IN_MICROSECOND = 1000;
constexpr int64_t MICROSEONDS_IN_SECOND_INT = 1000000;
constexpr int64_t NANOSECONDS_IN_SECOND_INT = 1000000000;

void timeval_to_timestamp(const struct timeval *tv, Timestamp *ts)
{
    if ((tv == nullptr) || (ts == nullptr))
        return;

    ts->integer = (uint32_t) tv->tv_sec + NTP_EPOCH_OFFSET;
    ts->fractional = (uint32_t) ((double) tv->tv_usec * ((double) (1ULL << 32) / MICROSECONDS_IN_SECOND));
}

void timespec_to_timestamp(const struct timespec *tv, Timestamp *ts)
{
    if ((tv == nullptr) || (ts == nullptr))
        return;

    /* Unix time to NTP */
    ts->integer = (uint32_t) tv->tv_sec + NTP_EPOCH_OFFSET;
    ts->fractional = (uint32_t) ((double) tv->tv_nsec * ((double) (1ULL << 32) / NANOSECONDS_IN_SECOND));
}

void timestamp_to_timeval(const Timestamp *ts, struct timeval *tv)
{
    if ((tv == nullptr) || (ts == nullptr))
        return;

    Timestamp ts_host_ord;

    ts_host_ord.integer = (ts->integer);
    ts_host_ord.fractional = (ts->fractional);
    tv->tv_sec = ts_host_ord.integer - NTP_EPOCH_OFFSET;
    tv->tv_usec = (suseconds_t) ((double) ts_host_ord.fractional * MICROSECONDS_IN_SECOND / (double) (1ULL << 32));
}

static void timestamp_to_timespec(const Timestamp *ts, struct timespec *tv)
{
    if ((tv == nullptr) || (ts == nullptr))
        return;

    Timestamp ts_host_ord;

    ts_host_ord.integer = (ts->integer);
    ts_host_ord.fractional = (ts->fractional);

    /* NTP to Unix time */
    tv->tv_sec = ts_host_ord.integer - NTP_EPOCH_OFFSET;
    tv->tv_nsec = (long) ((double) ts_host_ord.fractional * NANOSECONDS_IN_SECOND / (double) (1ULL << 32));
}

auto get_timestamp() -> Timestamp
{
    struct timespec tspec {};
    clock_gettime(CLOCK_REALTIME, &tspec);
    Timestamp ts;
    timespec_to_timestamp(&tspec, &ts);
    return ts;
}

auto timestamp_to_usec(const Timestamp *ts) -> uint64_t
{
    struct timeval tv {};
    timestamp_to_timeval(ts, &tv);
    return (uint64_t) tv.tv_sec * MICROSEONDS_IN_SECOND_INT + (uint64_t) tv.tv_usec;
}

auto timestamp_to_nsec(const Timestamp *ts) -> uint64_t
{
    struct timespec tv {};
    timestamp_to_timespec(ts, &tv);
    return (uint64_t) tv.tv_sec * NANOSECONDS_IN_SECOND_INT + (uint64_t) tv.tv_nsec;
}

auto nanosecondsToTimespec(uint64_t delay_epoch_nanoseconds) -> struct timespec {
    struct timespec ts {};
    ts.tv_sec = static_cast<time_t>(delay_epoch_nanoseconds / NANOSECONDS_IN_SECOND_INT);
    ts.tv_nsec = static_cast<long>(delay_epoch_nanoseconds % NANOSECONDS_IN_SECOND_INT);
    return ts;
}

auto get_usec() -> uint64_t
{
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return (uint64_t) tv.tv_sec * MICROSEONDS_IN_SECOND_INT + (uint64_t) tv.tv_usec;
}
/**
 * Session-Reflector implementations SHOULD fetch
      the TTL/Hop Limit value from the IP header of the packet,
      replacing the value of 255 set by the Session-Sender.  If an
      implementation does not fetch the actual TTL value (the only good
      reason not to do so is an inability to access the TTL field of
      arriving packets), it MUST set the Sender TTL value as 255.
 */
auto get_ip_header(msghdr hdr) -> IPHeader
{
    /* Get TTL/TOS values from IP header */
    uint8_t ttl = DEFAULT_TTL;
    uint8_t tos = 0;

#ifndef NO_MESSAGE_CONTROL
    struct cmsghdr *c_msg = nullptr;
    for (c_msg = CMSG_FIRSTHDR(&hdr); c_msg != nullptr; c_msg = (CMSG_NXTHDR(&hdr, c_msg))) {
        switch (c_msg->cmsg_level) {
        case IPPROTO_IP:
            switch (c_msg->cmsg_type) {
            case IP_TTL:
                ttl = *static_cast<uint8_t *>(CMSG_DATA(c_msg));
                break;
            case IP_TOS:
                tos = *static_cast<uint8_t *> CMSG_DATA(c_msg);
                break;
            default:
                std::cerr << "\tWarning, unexpected data of level " << c_msg->cmsg_level << " and type "
                          << c_msg->cmsg_type << std::endl;
                break;
            }
            break;

        case IPPROTO_IPV6:
            if (c_msg->cmsg_type == IPV6_HOPLIMIT) {
                ttl = *static_cast<uint8_t *> CMSG_DATA(c_msg);
            } else {
                std::cerr << "\tWarning, unexpected data of level " << c_msg->cmsg_level << " and type "
                          << c_msg->cmsg_type << std::endl;
            }
            break;

        default:
            break;
        }
    }
#else
    fprintf(stdout, "No message control on that platform, so no way to find IP options\n");
#endif
    IPHeader ipHeader = {ttl, tos};
    return ipHeader;
}

void get_kernel_timestamp(struct msghdr incoming_msg, struct timespec *incoming_timestamp)
{
    struct cmsghdr *cm{};
    for (cm = CMSG_FIRSTHDR(&incoming_msg); cm != nullptr; cm = CMSG_NXTHDR(&incoming_msg, cm)) {
        if (cm->cmsg_level != SOL_SOCKET)
            continue;
        switch (cm->cmsg_type) {
        case SO_TIMESTAMPNS:
        case SO_TIMESTAMPING:
            memcpy(incoming_timestamp, static_cast<void *>(CMSG_DATA(cm)), sizeof(struct timespec));
            break;
        case SO_TIMESTAMP: {
            struct timeval tv {};
            memcpy(&tv, static_cast<void *>(CMSG_DATA(cm)), sizeof(struct timeval));
            incoming_timestamp->tv_sec = tv.tv_sec;
            incoming_timestamp->tv_nsec =
                tv.tv_usec * NANOSECONDS_IN_MICROSECOND; // Convert microseconds to nanoseconds
            break;
        }
        default:
            /* Ignore other cmsg options */
            break;
        }
    }
}

void set_socket_options(int socket, uint8_t ip_ttl, uint8_t timeout_secs)
{
    /* Set socket options : timeout, IPTTL, IP_RECVTTL, IP_RECVTOS */
    uint8_t One = 1;
    int result = 0;

    /* Set Timeout */
    struct timeval timeout = {timeout_secs, 0}; // set timeout for 2 seconds
    /* Enable socket timestamping and set the timestamp resolution to nanoseconds */
    int flags = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_TIMESTAMPNS, &flags, sizeof(flags)) < 0)
        std::cerr << "ERROR: setsockopt SO_TIMESTAMPNS" << std::endl;

        /* Set receive UDP message timeout value */
#ifdef SO_RCVTIMEO
    if (timeout_secs != 0) {
        result = setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, static_cast<void *>(&timeout), sizeof(struct timeval));
        if (result != 0) {
            std::cerr << "[PROBLEM] Cannot set the timeout value for reception." << std::endl;
        }
    }

#else
    fprintf(stderr, "No way to set the timeout value for incoming packets on that platform.\n");
#endif

    /* Set IPTTL value to twamp standard: 255 */
#ifdef IP_TTL
    result = setsockopt(socket, IPPROTO_IP, IP_TTL, &ip_ttl, sizeof(ip_ttl));
    if (result != 0) {
        std::cerr << "[PROBLEM] Cannot set the TTL value for emission." << std::endl;
    }
#else
    fprintf(stderr, "No way to set the TTL value for leaving packets on that platform.\n");
#endif

    /* Set receive IP_TTL option */
#ifdef IP_RECVTTL
    result = setsockopt(socket, IPPROTO_IP, IP_RECVTTL, &One, sizeof(One));
    if (result != 0) {
        std::cerr << "[PROBLEM] Cannot set the socket option for TTL reception." << std::endl;
    }
#else
    fprintf(stderr, "No way to ask for the TTL of incoming packets on that platform.\n");
#endif
#ifdef IP_TOS
    result = setsockopt(socket, IPPROTO_IP, IP_TOS, &One, sizeof(One));
    if (result != 0) {
        std::cerr << "[PROBLEM] Cannot set the socket option for TOS." << std::endl;
    }
#else
    fprintf(stderr, "No way to ask for the TOS of incoming packets on that platform.\n");
#endif
    /* Set receive IP_TOS option */
#ifdef IP_RECVTOS
    result = setsockopt(socket, IPPROTO_IP, IP_RECVTOS, &One, sizeof(One));
    if (result != 0) {
        std::cerr << "[PROBLEM] Cannot set the socket option for TOS reception." << std::endl;
    }
#else
    fprintf(stderr, "No way to ask for the TOS of incoming packets on that platform.\n");
#endif
}
void set_socket_tos(int socket, uint8_t ip_tos)
{
    /* Set socket options : IP_TOS */
    int result = 0;

    /* Set IP TOS value */
#ifdef IP_TOS
    result = setsockopt(socket, IPPROTO_IP, IP_TOS, &ip_tos, sizeof(ip_tos));
    if (result != 0) {
        std::cerr << "[PROBLEM] Cannot set the TOS value for emission." << std::endl;
    }
#else
    fprintf(stderr, "No way to set the TOS value for leaving packets on that platform.\n");
#endif
}
auto isWithinEpsilon(double a, double b, double percentEpsilon) -> bool
{
    return (std::abs(a - b) <= (std::max(std::abs(a), std::abs(b)) * percentEpsilon));
}
auto ntohts(Timestamp ts) -> Timestamp
{
    Timestamp out = {};
    out.integer = ntohl(ts.integer);
    out.fractional = ntohl(ts.fractional);
    return out;
}
auto htonts(Timestamp ts) -> Timestamp
{
    Timestamp out = {};
    out.integer = htonl(ts.integer);
    out.fractional = htonl(ts.fractional);
    return out;
}

// Function to parse the IP:Port format
auto parseIPPort(const std::string &input, std::string &ip, uint16_t &port) -> bool
{
    size_t colon_pos = input.find(':');
    if (colon_pos == std::string::npos)
        return false;

    ip = input.substr(0, colon_pos);
    std::string port_str = input.substr(colon_pos + 1);

    char *endptr = nullptr;
    int64_t tmpport = strtol(port_str.c_str(), &endptr, 10);
    if (*endptr != '\0' || tmpport <= 0 || tmpport >= 65536) {
        return false;
    }
    port = (uint16_t) tmpport;
    return true;
}

auto parseIPv6Port(const std::string &input, std::string &ip, uint16_t &port) -> bool
{
    size_t lastColonPos = input.rfind(':');

    if (lastColonPos != std::string::npos) {
        ip = input.substr(0, lastColonPos);
        std::string port_str = input.substr(lastColonPos + 1);

        char *endptr = nullptr;
        int64_t tmpport = strtol(port_str.c_str(), &endptr, 10);
        if (*endptr != '\0' || tmpport <= 0 || tmpport >= 65536) {
            return false;
        }
        port = static_cast<uint16_t>(tmpport);
        return true;
    }

    return false;
}

auto make_msghdr(
    struct iovec *iov, size_t iov_len, struct sockaddr_in6 *addr, socklen_t addr_len, char *control, size_t control_len)
    -> struct msghdr {
    struct msghdr message = {};
    message.msg_name = addr;
    message.msg_namelen = addr_len;
    message.msg_iov = iov;
    message.msg_iovlen = iov_len;
    message.msg_control = control;
    message.msg_controllen = control_len;
    return message;
}

// Function to handle both IPv4 and IPv6
void parse_ip_address(struct msghdr sender_msg,
		      uint16_t *port,
		      char *host,
		      uint8_t ip_version)
{
    if (ip_version == IPV4) {
        auto *sock = static_cast<sockaddr_in *>(sender_msg.msg_name);
        if (inet_ntop(AF_INET, &(sock->sin_addr), host, INET_ADDRSTRLEN) == nullptr) {
            // Handle error
        }
        *port = ntohs(sock->sin_port);
    } else if (ip_version == IPV6) {
        auto *sock6 = static_cast<sockaddr_in6 *>(sender_msg.msg_name);
        if (inet_ntop(AF_INET6, &(sock6->sin6_addr), host, INET6_ADDRSTRLEN) == nullptr) {
            // Handle error
        }
        *port = ntohs(sock6->sin6_port);
    } else {
        // Handle invalid IP version
    }
}