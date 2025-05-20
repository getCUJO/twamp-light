//
// Created by vladim0105 on 12/17/21.
//

#include "Server.h"
#include "utils.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>

// Constants
constexpr uint16_t ERROR_ESTIMATE_DEFAULT_BITMAP =
	0x8001; // Sync = 1, Multiplier = 1

constexpr double MICROSECONDS_TO_SECONDS = 1e-6;

Server::Server(const Args &args)
	: args(args)
{
	// Construct socket address
	struct addrinfo hints = {};
	memset(&hints, 0, sizeof(hints));
	if (args.ip_version == 4) {
		hints.ai_family = AF_INET; // TODO IPv6
	} else if (args.ip_version == 6) {
		hints.ai_family = AF_INET6;
	} else {
		std::cerr << "Invalid IP version." << std::endl;
		std::exit(EXIT_FAILURE);
	}
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	struct addrinfo *res = nullptr;
	int err = getaddrinfo(args.local_host.empty() ? nullptr :
							args.local_host.c_str(),
			      args.local_port.c_str(),
			      &hints,
			      &res);
	if (err != 0) {
		std::cerr << "failed to resolve local socket address: " << err
			  << std::endl;
		std::exit(EXIT_FAILURE);
	}

	// Create the socket
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd == -1) {
		std::cerr << strerror(errno) << std::endl;
		std::exit(EXIT_FAILURE);
	}
	// Setup the socket options, to be able to receive TTL and TOS
	set_socket_options(fd, HDR_TTL, args.timeout);
	set_socket_tos(fd, args.snd_tos);
	// Bind the socket
	if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
		std::cerr << strerror(errno) << std::endl;
		std::exit(EXIT_FAILURE);
	}
	freeaddrinfo(res);
}

auto Server::listen() -> int
{
	// Read incoming datagrams
	uint32_t counter = 0;
	while (true) {
		if (args.num_samples != 0) {
			counter++;
			if (counter > args.num_samples) {
				break;
			}
		}
		std::array<char, sizeof(ClientPacket)>
			buffer{}; // We should only be receiving test_packets
		std::array<char, 1024> control{};
		struct sockaddr_in6 src_addr = {};

		std::array<struct iovec, 1> iov{};
		iov[0].iov_base = static_cast<void *>(buffer.data());
		iov[0].iov_len = buffer.size();
		timespec incoming_timestamp{};
		timespec *incoming_timestamp_ptr = &incoming_timestamp;

		struct msghdr message = make_msghdr(iov.data(),
						    1,
						    &src_addr,
						    sizeof(src_addr),
						    control.data(),
						    sizeof(control));

		ssize_t payload_len = recvmsg(fd, &message, 0);
		get_kernel_timestamp(message, incoming_timestamp_ptr);
		if (payload_len == -1) {
			if (errno == 11) {
				std::cerr << "Socket timed out." << std::endl;
				// std::exit(EXIT_FAILURE);
				return 11;
			}
			std::cerr << strerror(errno) << std::endl;
			return 1;
		}
		if ((message.msg_flags & MSG_TRUNC) != 0) {
			std::cout << "Datagram too large for buffer: truncated"
				  << std::endl;
		} else {
			auto *rec = static_cast<ClientPacket *>(
				static_cast<void *>(buffer.data()));
			handleTestPacket(rec,
					 message,
					 payload_len,
					 incoming_timestamp_ptr);
		}
	}
	return 0;
}

void Server::handleTestPacket(ClientPacket *packet,
			      msghdr sender_msg,
			      size_t payload_len,
			      timespec *incoming_timestamp)
{
	ReflectorPacket reflector_packet =
		craftReflectorPacket(packet, sender_msg, incoming_timestamp);
	std::array<char, INET6_ADDRSTRLEN> host = {};
	uint16_t port = 0;
	parse_ip_address(sender_msg, &port, host.data(), args.ip_version);
	// Overwrite and reuse the sender message with our own data and send it back, instead of creating a new one.
	uint64_t server_receive_time = 0;
	uint64_t server_send_time = 0;
	uint64_t initial_send_time = 0;
	int64_t client_server_delay = 0;
	Timestamp client_timestamp = ntohts(packet->send_time_data);
	Timestamp server_timestamp = ntohts(reflector_packet.server_time_data);
	Timestamp send_timestamp = ntohts(reflector_packet.send_time_data);
	client_server_delay = (int64_t)(timestamp_to_nsec(&server_timestamp) -
					timestamp_to_nsec(&client_timestamp));
	server_receive_time = timestamp_to_nsec(&server_timestamp);
	server_send_time = timestamp_to_nsec(&send_timestamp);
	initial_send_time = timestamp_to_nsec(&client_timestamp);

	/* Compute delays */
	auto internal_delay = (int64_t)(server_send_time - server_receive_time);

	MetricData data;
	data.payload_length = payload_len;
	data.packet = reflector_packet;
	data.client_server_delay_nanoseconds = client_server_delay;
	data.internal_delay_nanoseconds = internal_delay;
	data.receiving_port = std::stoi(args.local_port);
	data.sending_port = port;
	data.initial_send_time = initial_send_time;
	data.ip = std::string(host.data());
	printMetrics(data);
	struct msghdr message = sender_msg;

	std::array<struct iovec, 1> iov{};
	iov[0].iov_base = &reflector_packet;
	iov[0].iov_len = payload_len;
	message.msg_iov = iov.data();
	message.msg_iovlen = 1;
	message.msg_control = nullptr;
	message.msg_controllen = 0; // Set the control buffer size
	if (sendmsg(fd, &message, 0) == -1) {
		std::cerr << strerror(errno) << std::endl;
		return;
	}
}

auto Server::craftReflectorPacket(ClientPacket *clientPacket,
				  msghdr sender_msg,
				  timespec *incoming_timestamp)
	-> ReflectorPacket
{
	Timestamp server_timestamp = {};
	if (incoming_timestamp->tv_sec == 0 &&
	    incoming_timestamp->tv_nsec == 0) {
		// If the kernel timestamp is not available
		server_timestamp = get_timestamp();
	} else {
		timespec_to_timestamp(incoming_timestamp, &server_timestamp);
	}
	ReflectorPacket packet = {};
	packet.server_time_data = htonts(server_timestamp);
	packet.seq_number = clientPacket->seq_number;
	packet.sender_seq_number = clientPacket->seq_number;
	packet.sender_error_estimate = clientPacket->error_estimate;
	IPHeader ipHeader = get_ip_header(sender_msg);
	packet.sender_ttl = ipHeader.ttl;
	packet.sender_tos = ipHeader.tos;
	packet.error_estimate = htons(
		ERROR_ESTIMATE_DEFAULT_BITMAP); // Sync = 1, Multiplier = 1 Taken from TWAMP C implementation.
	packet.client_time_data = clientPacket->send_time_data;
	Timestamp send_timestamp = get_timestamp();
	packet.send_time_data = htonts(send_timestamp);

	return packet;
}

void Server::printMetrics(const MetricData &data)
{
	/* Sequence number */
	uint32_t snd_nb = ntohl(data.packet.sender_seq_number);
	uint32_t rcv_nb = ntohl(data.packet.seq_number);
	uint64_t client_send_time = data.initial_send_time;
	/* Sender TOS with ECN from FW TOS */
	uint8_t fw_tos = 0;
	uint8_t snd_tos = data.packet.sender_tos + (fw_tos & 0x3) -
			  (((fw_tos & 0x2) >> 1) & (fw_tos & 0x1));
	if (!header_printed) {
		std::cout << "Time" << args.sep << "IP" << args.sep << "Snd#"
			  << args.sep << "Rcv#" << args.sep << "SndPort"
			  << args.sep << "RscPort" << args.sep << "FW_TTL"
			  << args.sep << "SndTOS" << args.sep << "FW_TOS"
			  << args.sep << "IntD" << args.sep << "FWD" << args.sep
			  << "PLEN" << args.sep << "\n";
		header_printed = true;
	}
	std::cout << std::fixed << client_send_time << args.sep << data.ip
		  << args.sep << snd_nb << args.sep << rcv_nb << args.sep
		  << data.sending_port << args.sep << data.receiving_port
		  << args.sep << unsigned(data.packet.sender_ttl) << args.sep
		  << snd_tos << args.sep
		  << (double)data.client_server_delay_nanoseconds *
			     MICROSECONDS_TO_SECONDS
		  << args.sep
		  << (double)data.internal_delay_nanoseconds *
			     MICROSECONDS_TO_SECONDS
		  << args.sep
		  << (double)data.client_server_delay_nanoseconds *
			     MICROSECONDS_TO_SECONDS
		  << args.sep << std::to_string(data.payload_length) << "\n";
}
