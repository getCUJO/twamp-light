//
// Created by vladim0105 on 12/17/21.
//
#include "utils.hpp"
#include <string>

struct Args {
    std::string local_host;
    std::string local_port = "443";
    uint32_t num_samples = 0;
    uint8_t timeout = 0;
    uint8_t snd_tos = 0;
    uint8_t ip_version = 4;
    char sep = ',';
};
struct MetricData {
    std::string ip;
    uint16_t sending_port = 0;
    uint16_t receiving_port = 0;
    uint16_t payload_length = 0;
    int64_t client_server_delay_nanoseconds = 0;
    int64_t internal_delay_nanoseconds = 0;
    uint64_t initial_send_time = 0;
    ReflectorPacket packet;
};
class Server {
  public:
    explicit Server(const Args &args);
    Server(const Server &other) = default;
    auto operator=(const Server &other) -> Server & = default;
    Server(Server &&other) noexcept = default;
    auto operator=(Server &&other) noexcept -> Server & = default;
    auto listen() -> int;
    ~Server();

  private:
    int fd;
    bool header_printed = false;

    Args args;
    void handleTestPacket(ClientPacket *packet, msghdr sender_msg, ssize_t payload_len, timespec *incoming_timestamp);
    void printMetrics(const MetricData &data);
    static auto craftReflectorPacket(ClientPacket *clientPacket, msghdr sender_msg, timespec *incoming_timestamp)
        -> ReflectorPacket;
};