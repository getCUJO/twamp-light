//
// Created by vladim0105 on 12/15/21.
//

#include "Client.h"
#include "json.hpp"
#include "utils.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <unistd.h>

constexpr int COLLATOR_SLEEP_DURATION_MICROSECONDS = 100;
constexpr double NANOSECONDS_TO_MILLISECONDS = 1e-6;
constexpr double SYNC_DELAY_EPSILON_THRESHOLD =
	0.01; // Threshold for comparison
constexpr int64_t MICROSECONDS_IN_MILLISECOND = 1000;
constexpr int64_t MILLISECONDS_IN_SECOND = 1000;
constexpr int64_t NANOSECONDS_IN_MICROSECOND = 1000;
constexpr int64_t NANOSECONDS_IN_SECOND = 1000000000;
constexpr int64_t MAXINT64 = std::numeric_limits<int64_t>::max();

using Clock = std::chrono::system_clock;

Client::Client(const Args &args)
	: start_time(time(nullptr))
	, raw_data_list()
	, args(args)
{
	// Construct remote socket address
	struct addrinfo hints {};
	memset(&hints, 0, sizeof(hints));
	if (args.ip_version == IPV4) {
		hints.ai_family = AF_INET;
	} else if (args.ip_version == IPV6) {
		hints.ai_family = AF_INET6;
	} else {
		std::cerr << "Invalid IP version." << std::endl;
		std::exit(EXIT_FAILURE);
	}
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
	// Resize the remote_address_info vector based on the number of remote hosts
	remote_address_info.resize(args.remote_hosts.size());
	int i = 0;
	for (const auto &remote_host : args.remote_hosts) {
		std::string port;
		try {
			// Convert port number to string
			port = std::to_string(args.remote_ports.at(i));
		} catch (std::out_of_range &e) {
			std::cerr << "Not enough remote ports provided"
				  << std::endl;
			std::exit(EXIT_FAILURE);
		}
		int err = getaddrinfo(remote_host.c_str(),
				      port.c_str(),
				      &hints,
				      &remote_address_info[i]);
		if (err != 0) {
			std::cerr << "failed to resolve remote socket address: "
				  << err;
			std::exit(EXIT_FAILURE);
		}
		i++;
	}
	int err2 = getaddrinfo(
		args.local_host.empty() ? nullptr : args.local_host.c_str(),
		args.local_port.c_str(),
		&hints,
		&local_address_info);
	if (err2 != 0) {
		std::cerr << "failed to resolve local socket address: " << err2;
		std::exit(EXIT_FAILURE);
	}
	// Create the socket
	fd = socket(remote_address_info[0]->ai_family,
		    remote_address_info[0]->ai_socktype,
		    remote_address_info[0]->ai_protocol);
	if (fd == -1) {
		std::cerr << strerror(errno) << std::endl;
		throw std::runtime_error("Failed to create socket: " +
					 std::string(strerror(errno)));
	}
	// Setup the socket options, to be able to receive TTL and TOS
	set_socket_options(fd, HDR_TTL, args.timeout);
	set_socket_tos(fd, args.snd_tos);
	// Bind the socket to a local port
	if (bind(fd,
		 local_address_info->ai_addr,
		 local_address_info->ai_addrlen) == -1) {
		std::cerr << strerror(errno) << std::endl;
		throw std::runtime_error("Failed to bind socket: " +
					 std::string(strerror(errno)));
	}
	// Initialize the stats
	stats_RTT = sqa_stats_create();
	if (stats_RTT == nullptr) {
		std::cerr << "Failed to allocate memory for stats_RTT"
			  << std::endl;
		throw std::runtime_error(
			"Failed to allocate memory for stats_RTT");
	}
	stats_internal = sqa_stats_create();
	if (stats_internal == nullptr) {
		std::cerr << "Failed to allocate memory for stats_internal"
			  << std::endl;
		throw std::runtime_error(
			"Failed to allocate memory for stats_internal");
	}
	stats_client_server = sqa_stats_create();
	if (stats_client_server == nullptr) {
		std::cerr << "Failed to allocate memory for stats_client_server"
			  << std::endl;
		throw std::runtime_error(
			"Failed to allocate memory for stats_client_server");
	}
	stats_server_client = sqa_stats_create();
	if (stats_server_client == nullptr) {
		std::cerr << "Failed to allocate memory for stats_server_client"
			  << std::endl;
		throw std::runtime_error(
			"Failed to allocate memory for stats_server_client");
	}
}

Client::~Client()
{
	sqa_stats_destroy(stats_RTT);
	sqa_stats_destroy(stats_internal);
	sqa_stats_destroy(stats_client_server);
	sqa_stats_destroy(stats_server_client);
	for (auto &addrinfo : remote_address_info) {
		if (addrinfo != nullptr) {
			freeaddrinfo(addrinfo);
		}
	}
	if (local_address_info != nullptr) {
		freeaddrinfo(local_address_info);
	}
}

auto decode_observation_point(ObservationPoints observation_point)
	-> std::string
{
	std::string retval = {};
	if (observation_point == ObservationPoints::CLIENT_SEND) {
		retval = "client_send_time";
	} else if (observation_point == ObservationPoints::SERVER_RECEIVE) {
		retval = "server_receive_time";
	} else if (observation_point == ObservationPoints::SERVER_SEND) {
		retval = "server_send_time";
	} else if (observation_point == ObservationPoints::CLIENT_RECEIVE) {
		retval = "client_receive_time";
	} else {
		retval = "unknown";
	}
	return retval;
}

/* The packet generation function */
void Client::runSenderThread()
{
	uint32_t index = 0;
	std::random_device
		rd; // Will be used to obtain a seed for the random number engine
	std::mt19937 gen(
		rd()); // Standard mersenne_twister_engine seeded with rd()
	constexpr double MAX_DELAY_MICROSECONDS =
		10000000.0; // Maximum delay in microseconds
	std::exponential_distribution<> d(
		1.0 /
		(static_cast<double>(args.mean_inter_packet_delay_ms) *
		 MICROSECONDS_IN_MILLISECOND)); // Lambda is 1.0/mean (in microseconds)
	while (args.num_samples == 0 || index < args.num_samples ||
	       (args.runtime != 0 &&
		time(nullptr) - this->start_time < args.runtime)) {
		size_t payload_len = *select_randomly(args.payload_lens.begin(),
						      args.payload_lens.end(),
						      args.seed);
		uint32_t delay = 0;
		if (args.constant_inter_packet_delay) {
			delay = args.mean_inter_packet_delay_ms *
				MICROSECONDS_IN_MILLISECOND;
		} else {
			delay = std::max(
				static_cast<uint32_t>(std::min(
					d(gen), MAX_DELAY_MICROSECONDS)),
				static_cast<uint32_t>(0));
		}
		usleep(delay);
		try {
			Timestamp sent_time = sendPacket(index, payload_len);
			if (first_packet_sent_epoch_nanoseconds == 0) {
				first_packet_sent_epoch_nanoseconds =
					timestamp_to_nsec(&sent_time);
			}
			last_packet_sent_epoch_nanoseconds =
				timestamp_to_nsec(&sent_time);
			if (this->collator_started != 0) {
				auto obs = std::make_shared<QEDObservation>(
					ObservationPoints::CLIENT_SEND,
					timestamp_to_nsec(&sent_time),
					index,
					payload_len);

				enqueue_observation(obs);
			}
		} catch (
			const std::exception &e) { // catch error from sendPacket
			std::cerr << e.what() << std::endl;
		}
		index++;
	}
	this->sending_completed = time(nullptr);
}

/* Receives and processes the reflected
    packets from the server side.*/
void Client::runReceiverThread()
{
	/* run forever if num_samples is 0, 
    otherwise run until all packets have been received (or timed out) */
	while ((args.num_samples == 0 || this->sending_completed == 0 ||
		(this->received_packets < this->sent_packets &&
		 time(nullptr) - this->sending_completed < args.timeout))) {
		awaitAndHandleResponse();
	}
}

void Client::process_observation(const std::shared_ptr<QEDObservation> &obs)
{
	// Look for the observation in the raw_data deque
	std::shared_ptr<RawData> entry = nullptr;
	int made_new_entry = 0;
	int found = 0;
	for (auto &it : raw_data_list) {
		entry = it;
		if (entry->getPacketId() == obs->getPacketId()) {
			found = 1;
			break;
		}
	}
	if (found == 0) {
		// Didn't find the entry, so create a new one
		const Timestamp now_ts = get_timestamp();
		entry = std::make_shared<RawData>(timestamp_to_nsec(&now_ts),
						  obs->getPacketId());
		made_new_entry = 1;
	}
	// Update the entry with the observation data
	entry->setPayloadLen(obs->getPayloadLen());
	switch (obs->getObservationPoint()) {
	case ObservationPoints::CLIENT_SEND:
		entry->setClientSendEpochNanoseconds(
			obs->getEpochNanoseconds());
		break;
	case ObservationPoints::SERVER_RECEIVE:
		entry->setServerReceiveEpochNanoseconds(
			obs->getEpochNanoseconds());
		break;
	case ObservationPoints::SERVER_SEND:
		entry->setServerSendEpochNanoseconds(
			obs->getEpochNanoseconds());
		break;
	case ObservationPoints::CLIENT_RECEIVE:
		entry->setClientReceiveEpochNanoseconds(
			obs->getEpochNanoseconds());
		break;
	default:
		break;
	}
	// If the entry is new, add it to the raw_data list
	if (made_new_entry > 0) {
		// Add the entry to the raw_data list
		raw_data_list.addObservation(entry);
	}
}

void Client::check_if_oldest_packet_should_be_processed()
{
	// Check the timestamp on the oldest entry in raw_data and print it if it is old enough
	std::shared_ptr<RawData> oldest_raw_data =
		raw_data_list.getOldestEntry();
	Timestamp now = get_timestamp();
	uint64_t now_nanoseconds = timestamp_to_nsec(&now);
	int oldest_entry_is_complete = 0;
	if (oldest_raw_data != nullptr &&
	    oldest_raw_data->getClientSendEpochNanoseconds() > 0 &&
	    oldest_raw_data->getServerReceiveEpochNanoseconds() > 0 &&
	    oldest_raw_data->getServerSendEpochNanoseconds() > 0 &&
	    oldest_raw_data->getClientReceiveEpochNanoseconds() > 0) {
		oldest_entry_is_complete = 1;
	}
	if (oldest_raw_data != nullptr &&
	    (oldest_entry_is_complete > 0 ||
	     (now_nanoseconds - oldest_raw_data->getAddedAtEpochNanoseconds()) >
		     args.timeout * NANOSECONDS_IN_SECOND)) {
		// The oldest entry is old enough to be processed
		aggregateRawData(oldest_raw_data);

		if (args.print_format == "raw") {
			std::cout
				<< oldest_raw_data->getPacketId() << args.sep
				<< oldest_raw_data->getPayloadLen() << args.sep
				<< oldest_raw_data
					   ->getClientSendEpochNanoseconds()
				<< args.sep
				<< oldest_raw_data
					   ->getServerReceiveEpochNanoseconds()
				<< args.sep
				<< oldest_raw_data
					   ->getServerSendEpochNanoseconds()
				<< args.sep
				<< oldest_raw_data
					   ->getClientReceiveEpochNanoseconds()
				<< "\n";
		}
		// Remove the oldest entry from the raw_data list
		raw_data_list.popObservation();
	}
	if (oldest_raw_data == nullptr && this->sending_completed > 0) {
		// All the packets have been sent and all the responses have been received or timed out
		// Close the thread
		collator_finished = 1;
	}
}

/* Processes observations recorded by the sender and the receiver */
void Client::runCollatorThread()
{
	this->collator_started = 1;
	// Consumes the observation queue and generates a table.
	// Uses semaphore to wake the thread only when there are observations to consume.
	while (collator_finished == 0) {
		std::shared_ptr<QEDObservation> tmp_obs;

		if (!observation_list.isEmpty()) {
			// Retrieve and remove the first observation from the list
			tmp_obs = observation_list.popObservation();
			if (tmp_obs) {
				process_observation(tmp_obs);
			}
		} else {
			check_if_oldest_packet_should_be_processed();
			usleep(COLLATOR_SLEEP_DURATION_MICROSECONDS);
		}
	}
}

void Client::printRawDataHeader() const
{
	// Print a header
	std::cout << "packet_id" << args.sep << "payload_len" << args.sep
		  << "client_send_epoch_nanoseconds" << args.sep
		  << "server_receive_epoch_nanoseconds" << args.sep
		  << "server_send_epoch_nanoseconds" << args.sep
		  << "client_receive_epoch_nanoseconds"
		  << "\n";
	(void)fflush(stdout);
}

void Client::aggregateRawData(const std::shared_ptr<RawData> &oldest_raw_data)
{
	// Compute the delays (without clock correction), and add them to the sqa_stats
	timespec client_server_delay = {};
	if (oldest_raw_data->getClientSendEpochNanoseconds() > 0 &&
	    oldest_raw_data->getServerReceiveEpochNanoseconds() > 0) {
		client_server_delay = nanosecondsToTimespec(
			oldest_raw_data->getServerReceiveEpochNanoseconds() -
			oldest_raw_data->getClientSendEpochNanoseconds());
		sqa_stats_add_sample(this->stats_client_server,
				     &client_server_delay);
	}
	timespec server_client_delay = {};
	if (oldest_raw_data->getServerSendEpochNanoseconds() > 0 &&
	    oldest_raw_data->getClientReceiveEpochNanoseconds() > 0 &&
	    oldest_raw_data->getServerReceiveEpochNanoseconds() > 0 &&
	    oldest_raw_data->getClientSendEpochNanoseconds() > 0) {
		server_client_delay = nanosecondsToTimespec(
			oldest_raw_data->getClientReceiveEpochNanoseconds() -
			oldest_raw_data->getServerSendEpochNanoseconds());
		sqa_stats_add_sample(this->stats_server_client,
				     &server_client_delay);
	} else {
		// We don't know where the packet was lost, so update all loss counters
		sqa_stats_count_loss(this->stats_client_server);
		sqa_stats_count_loss(this->stats_internal);
		sqa_stats_count_loss(this->stats_server_client);
		sqa_stats_count_loss(this->stats_RTT);
	}
	timespec internal_delay{};
	timespec rtt_delay{};

	tspecminus(&server_client_delay, &client_server_delay, &internal_delay);
	if (tspecmsec(&internal_delay) != 0) {
		sqa_stats_add_sample(this->stats_internal, &internal_delay);
	}

	tspecplus(&client_server_delay, &server_client_delay, &rtt_delay);
	if (tspecmsec(&rtt_delay) != 0) {
		sqa_stats_add_sample(this->stats_RTT, &rtt_delay);
	}
}

auto Client::getSentPackets() const -> int
{
	return sent_packets;
}

auto Client::sendPacket(uint32_t idx, size_t payload_len) -> Timestamp
{
	// Send the UDP packet
	ClientPacket senderPacket = craftSenderPacket(idx);
	std::array<struct iovec, 1> iov{};
	iov[0].iov_base = &senderPacket;
	iov[0].iov_len = payload_len;
	for (const auto &rai : remote_address_info) {
		struct msghdr message = {};
		message.msg_name = rai->ai_addr;
		message.msg_namelen = rai->ai_addrlen;
		message.msg_iov = iov.data();
		message.msg_iovlen = 1;
		message.msg_control = nullptr;
		message.msg_controllen = 0;
		if (sendmsg(fd, &message, 0) == -1) {
			std::cerr << strerror(errno) << std::endl;
			throw std::runtime_error(std::string(
				"Sending UDP message failed with error."));
		}
		this->sent_packets += 1;
	}
	return ntohts(senderPacket.send_time_data);
}

auto Client::craftSenderPacket(uint32_t idx) -> ClientPacket
{
	constexpr uint16_t ERROR_ESTIMATE_DEFAULT_BITMAP =
		0x8001; // Sync = 1, Multiplier = 1
	ClientPacket packet = {};
	packet.seq_number = htonl(idx);
	packet.error_estimate = htons(
		ERROR_ESTIMATE_DEFAULT_BITMAP); // Sync = 1, Multiplier = 1.
	auto ts = get_timestamp();
	packet.send_time_data = htonts(ts);
	return packet;
}

auto Client::awaitAndHandleResponse() -> bool
{
	// Read incoming datagram
	std::array<char, sizeof(ReflectorPacket)>
		buffer{}; // We should only be receiving ReflectorPackets
	std::array<char, 2048> control{};
	struct sockaddr_in6 src_addr {};

	struct iovec iov {};
	iov.iov_base = buffer.data();
	iov.iov_len = sizeof(buffer);

	timespec incoming_timestamp = {0, 0};
	timespec *incoming_timestamp_ptr = &incoming_timestamp;

	struct msghdr incoming_msg = make_msghdr(&iov,
						 1,
						 &src_addr,
						 sizeof(src_addr),
						 control.data(),
						 sizeof(control));

	ssize_t count = recvmsg(fd, &incoming_msg, MSG_WAITALL);
#ifdef KERNEL_TIMESTAMP_DISABLED_IN_CLIENT
#else
	get_kernel_timestamp(incoming_msg, incoming_timestamp_ptr);
#endif
	if (count == -1) {
		//std::cerr << strerror(errno) << std::endl;
		return false;
	}
	if ((incoming_msg.msg_flags & MSG_TRUNC) != 0) {
		return false;
	}
	auto *rec = static_cast<ReflectorPacket *>(
		static_cast<void *>(buffer.data()));
	handleReflectorPacket(rec, incoming_msg, count, incoming_timestamp_ptr);
	return true;
}

struct TimeData {
	int64_t internal_delay; // Internal server delay in nanoseconds
	int64_t server_client_delay; // Server to client delay in nanoseconds
	int64_t client_server_delay; // Client to server delay in nanoseconds
	uint64_t rtt; // Round trip time in nanoseconds
	uint64_t client_send_time; // Client send time in nanoseconds since the epoch
	uint64_t server_receive_time; // Server receive time in nanoseconds since the epoch
	uint64_t server_send_time; // Server send time in nanoseconds since the epoch
};

auto computeTimeData(uint64_t client_receive_time,
		     ReflectorPacket *reflectorPacket) -> TimeData
{
	TimeData timeData{};
	if (client_receive_time > MAXINT64) {
		// Issue a warning if the client receive time is too large
		std::cerr
			<< "Client receive time is too large. Clock must be wrong or it's the year 2262."
			<< client_receive_time << std::endl;
	}
	auto client_timestamp = ntohts(reflectorPacket->client_time_data);
	auto server_timestamp = ntohts(reflectorPacket->server_time_data);
	auto send_timestamp = ntohts(reflectorPacket->send_time_data);

	timeData.client_send_time = timestamp_to_nsec(&client_timestamp);
	timeData.server_receive_time = timestamp_to_nsec(&server_timestamp);
	timeData.server_send_time = timestamp_to_nsec(&send_timestamp);
	if (timeData.server_send_time > MAXINT64) {
		// Issue a warning if the server send time is too large
		std::cerr
			<< "Server send time is too large. Clock must be wrong or it's the year 2262."
			<< timeData.server_send_time << std::endl;
	}
	if (timeData.client_send_time > MAXINT64) {
		// Issue a warning if the client send time is too large
		std::cerr
			<< "Client send time is too large. Clock must be wrong or it's the year 2262."
			<< timeData.client_send_time << std::endl;
	}
	if (timeData.server_receive_time > MAXINT64) {
		// Issue a warning if the server receive time is too large
		std::cerr
			<< "Server receive time is too large. Clock must be wrong or it's the year 2262."
			<< timeData.server_receive_time << std::endl;
	}
	timeData.internal_delay =
		static_cast<int64_t>(timeData.server_send_time) -
		static_cast<int64_t>(timeData.server_receive_time);
	timeData.client_server_delay =
		static_cast<int64_t>(timeData.server_receive_time) -
		static_cast<int64_t>(timeData.client_send_time);
	timeData.server_client_delay =
		static_cast<int64_t>(client_receive_time) -
		static_cast<int64_t>(timeData.server_send_time);
	timeData.rtt = client_receive_time - timeData.client_send_time;

	return timeData;
}

void populateMetricData(MetricData &data,
			ReflectorPacket *reflectorPacket,
			const IPHeader &ipHeader,
			const std::string &host,
			uint16_t local_port,
			uint16_t port,
			ssize_t payload_len,
			const TimeData &timeData,
			struct sqa_stats *stats)
{
	data.ip = host;
	data.sending_port = local_port;
	data.receiving_port = port;
	data.packet = *reflectorPacket;
	data.ipHeader = ipHeader;
	data.initial_send_time = timeData.client_send_time;
	data.payload_length = payload_len;
	data.internal_delay_nanoseconds = timeData.internal_delay;
	data.server_client_delay_nanoseconds = timeData.server_client_delay;
	data.client_server_delay_nanoseconds = timeData.client_server_delay;
	data.rtt_delay_nanoseconds = timeData.rtt;
	data.packets_sent = uint64_t(stats->number_of_samples);
	data.packets_lost = uint64_t(stats->number_of_lost_packets);
}

void Client::enqueue_observation(const std::shared_ptr<QEDObservation> &obs)
{
	observation_list.addObservation(obs);
}

void Client::handleReflectorPacket(ReflectorPacket *reflectorPacket,
				   msghdr msghdr,
				   ssize_t payload_len,
				   timespec *incoming_timestamp)
{
	Timestamp client_receive_time;
	uint64_t incoming_timestamp_nanoseconds = 0;
	if (incoming_timestamp->tv_sec == 0 &&
	    incoming_timestamp->tv_nsec == 0) {
		// If the kernel timestamp is not available, use the client receive time
		client_receive_time = get_timestamp();
		incoming_timestamp_nanoseconds =
			timestamp_to_nsec(&client_receive_time);
	} else {
		// Convert timespec to timestamp
		incoming_timestamp_nanoseconds =
			(uint64_t)incoming_timestamp->tv_sec *
				NANOSECONDS_IN_SECOND +
			incoming_timestamp->tv_nsec;
	}
	last_packet_received_epoch_nanoseconds = incoming_timestamp_nanoseconds;

	Timestamp server_receive_time =
		ntohts(reflectorPacket->server_time_data);
	Timestamp server_send_time = ntohts(reflectorPacket->send_time_data);
	//IPHeader ipHeader = get_ip_header(msghdr);
	//uint8_t tos = ipHeader.tos;
	// sockaddr_in *sock = ((sockaddr_in *)msghdr.msg_name);
	// std::string host = inet_ntoa(sock->sin_addr);
	// uint16_t  port = ntohs(sock->sin_port);
	// uint16_t local_port = atoi(args.local_port.c_str());
	uint32_t packet_id = ntohl(reflectorPacket->seq_number);
	this->last_received_packet_id = packet_id;
	this->received_packets += 1;
	if (this->collator_started != 0) {
		auto obs1 = std::make_shared<QEDObservation>(
			ObservationPoints::CLIENT_RECEIVE,
			incoming_timestamp_nanoseconds,
			packet_id,
			payload_len);
		auto obs2 = std::make_shared<QEDObservation>(
			ObservationPoints::SERVER_RECEIVE,
			timestamp_to_nsec(&server_receive_time),
			packet_id,
			payload_len);
		auto obs3 = std::make_shared<QEDObservation>(
			ObservationPoints::SERVER_SEND,
			timestamp_to_nsec(&server_send_time),
			packet_id,
			payload_len);

		// Queue all observations in the FIFO to the collator
		enqueue_observation(obs3);
		enqueue_observation(obs2);
		enqueue_observation(obs1);
	}
	if (args.print_format == "legacy") {
		printReflectorPacket(reflectorPacket,
				     msghdr,
				     payload_len,
				     incoming_timestamp_nanoseconds,
				     this->stats_client_server);
	}
}

void Client::printReflectorPacket(ReflectorPacket *reflectorPacket,
				  msghdr msghdr,
				  ssize_t payload_len,
				  uint64_t incoming_timestamp_nanoseconds,
				  struct sqa_stats *stats)
{
	uint64_t client_receive_time = incoming_timestamp_nanoseconds;
	IPHeader ipHeader = get_ip_header(msghdr);
	std::array<char, INET6_ADDRSTRLEN> host = {};
	uint16_t port = 0;
	parse_ip_address(msghdr, &port, host.data(), args.ip_version);
	uint16_t local_port = 0;
	try {
		local_port = static_cast<uint16_t>(std::stoi(args.local_port));
	} catch (const std::invalid_argument &e) {
		std::cerr << "Invalid local port: " << args.local_port
			  << std::endl;
		std::exit(EXIT_FAILURE);
	} catch (const std::out_of_range &e) {
		std::cerr << "Local port out of range: " << args.local_port
			  << std::endl;
		std::exit(EXIT_FAILURE);
	}
	TimeData timeData =
		computeTimeData(client_receive_time, reflectorPacket);

	MetricData data;
	populateMetricData(data,
			   reflectorPacket,
			   ipHeader,
			   host.data(),
			   local_port,
			   port,
			   payload_len,
			   timeData,
			   stats);

	if (args.print_RTT_only) {
		std::cout << std::fixed
			  << (double)timeData.rtt / NANOSECONDS_TO_MILLISECONDS
			  << "\n";
		(void)fflush(stdout);
	} else {
		printMetrics(data);
	}
}

void Client::printHeader() const
{
	if (args.print_format == "legacy") {
		std::cout << "Time" << args.sep << "IP" << args.sep << "Snd#"
			  << args.sep << "Rcv#" << args.sep << "SndPort"
			  << args.sep << "RscPort" << args.sep << "Sync"
			  << args.sep << "FW_TTL" << args.sep << "SW_TTL"
			  << args.sep << "SndTOS" << args.sep << "FW_TOS"
			  << args.sep << "SW_TOS" << args.sep << "RTT"
			  << args.sep << "IntD" << args.sep << "FWD" << args.sep
			  << "BWD" << args.sep << "PLEN";
		if (args.print_lost_packets) {
			std::cout << args.sep << "SENT" << args.sep << "LOST";
		}
		std::cout << "\n";
	} else if (args.print_format == "raw") {
		std::cout << "packet_id" << args.sep << "payload_len"
			  << args.sep << "client_send_epoch_nanoseconds"
			  << args.sep << "server_receive_epoch_nanoseconds"
			  << args.sep << "server_send_epoch_nanoseconds"
			  << args.sep << "client_receive_epoch_nanoseconds"
			  << "\n";
	} else if (args.print_format == "clockcorrected") {
		std::cout << "packet_id" << args.sep << "payload_len"
			  << args.sep << "packet_generated_timestamp"
			  << args.sep << "delay_to_server" << args.sep
			  << "delay_to_server_response" << args.sep
			  << "delay_round_trip"
			  << "\n";
	}
	(void)fflush(stdout);
}

void Client::printMetrics(const MetricData &data) const
{
	char sync = 'N';
	uint64_t estimated_rtt_nanoseconds =
		data.client_server_delay_nanoseconds +
		data.server_client_delay_nanoseconds +
		data.internal_delay_nanoseconds;
	if (isWithinEpsilon((double)data.rtt_delay_nanoseconds *
				    NANOSECONDS_TO_MILLISECONDS,
			    (double)estimated_rtt_nanoseconds *
				    NANOSECONDS_TO_MILLISECONDS,
			    SYNC_DELAY_EPSILON_THRESHOLD)) {
		sync = 'Y';
	}
	if ((data.client_server_delay_nanoseconds < 0) ||
	    (data.server_client_delay_nanoseconds < 0)) {
		sync = 'N';
	}
	/*Sequence number */
	uint32_t rcv_sn = ntohl(data.packet.seq_number);
	uint32_t snd_sn = ntohl(data.packet.sender_seq_number);

	std::cout
		<< std::fixed << data.initial_send_time << args.sep << data.ip
		<< args.sep << snd_sn << args.sep << rcv_sn << args.sep
		<< data.sending_port << args.sep << data.receiving_port
		<< args.sep << sync << args.sep
		<< unsigned(data.packet.sender_ttl) << args.sep
		<< unsigned(data.ipHeader.ttl) << args.sep
		<< unsigned(data.packet.sender_tos) << args.sep << '-'
		<< args.sep << unsigned(data.ipHeader.tos) << args.sep
		<< (double)data.rtt_delay_nanoseconds *
			   NANOSECONDS_TO_MILLISECONDS // Nanoseconds to milliseconds
		<< args.sep
		<< (double)data.internal_delay_nanoseconds *
			   NANOSECONDS_TO_MILLISECONDS
		<< args.sep
		<< (double)data.client_server_delay_nanoseconds *
			   NANOSECONDS_TO_MILLISECONDS
		<< args.sep
		<< (double)data.server_client_delay_nanoseconds *
			   NANOSECONDS_TO_MILLISECONDS
		<< args.sep << data.payload_length;
	if (args.print_lost_packets) {
		std::cout << args.sep << data.packets_sent << args.sep
			  << data.packets_lost;
	}
	std::cout << "\n";
	(void)fflush(stdout);
}

void Client::print_lost_packet(uint32_t packet_id,
			       uint64_t initial_send_time,
			       uint16_t payload_len) const
{
	std::cout << std::fixed << initial_send_time
		  << args.sep
		  //<< data.ip
		  << args.sep << packet_id
		  << args.sep
		  //<< rcv_sn
		  << args.sep
		  //<< data.sending_port
		  << args.sep
		  //<< data.receiving_port
		  << args.sep
		  //<< sync
		  << args.sep
		  //<< unsigned(data.packet.sender_ttl)
		  << args.sep
		  //<< unsigned(data.ipHeader.ttl)
		  << args.sep
		  //<< unsigned(data.packet.sender_tos)
		  << args.sep << '-'
		  << args.sep
		  //<< unsigned(data.ipHeader.tos)
		  << args.sep
		  //<<(double) data.rtt_delay * 1e-3
		  << args.sep
		  //<<(double) data.internal_delay* 1e-3
		  << args.sep
		  //<< (double) data.client_server_delay * 1e-3
		  << args.sep
		  //<< (double) data.server_client_delay * 1e-3
		  << args.sep << payload_len
		  << args.sep
		  //<< data.packet_loss
		  << "\n";
	(void)fflush(stdout);
}

template <typename Func>
void Client::printSummaryLine(const std::string &label, Func func)
{
	std::cout << " " << std::left << std::setw(10) << label
		  << std::setprecision(6);
	std::cout << func(this->stats_RTT) << " s      ";
	std::cout << func(this->stats_client_server) << " s      ";
	std::cout << func(this->stats_server_client) << " s      ";
	std::cout << func(this->stats_internal) << " s\n";
	(void)fflush(stdout);
}

void Client::printStats(int packets_sent)
{
	// printLostPackets();
	std::cout << std::fixed;
	std::cout << "Time spent generating packets: "
		  << (double)(Client::last_packet_sent_epoch_nanoseconds -
			      Client::first_packet_sent_epoch_nanoseconds) /
			     NANOSECONDS_IN_SECOND
		  << " s\n";
	Timestamp now_ts = get_timestamp();
	std::cout << "Total time elapsed: "
		  << (double)(timestamp_to_nsec(&now_ts) -
			      Client::first_packet_sent_epoch_nanoseconds) /
			     NANOSECONDS_IN_SECOND
		  << " s\n";
	std::cout << "Packets sent: " << packets_sent << "\n";
	std::cout << "Packets lost: "
		  << sqa_stats_get_number_of_lost_packets(Client::stats_RTT)
		  << "\n";
	std::cout << "Packet loss: "
		  << sqa_stats_get_loss_percentage(Client::stats_RTT) << "%\n";
	std::cout
		<< "           RTT             FWD             BWD             Internal\n";
	(void)fflush(stdout);

	auto printPercentileLine = [&](const std::string &label,
				       double percentile) {
		std::cout << " " << std::left << std::setw(10) << label
			  << std::setprecision(6);
		std::cout << sqa_stats_get_percentile(Client::stats_RTT,
						      percentile)
			  << " s      ";
		std::cout
			<< sqa_stats_get_percentile(Client::stats_client_server,
						    percentile)
			<< " s      ";
		std::cout
			<< sqa_stats_get_percentile(Client::stats_server_client,
						    percentile)
			<< " s      ";
		std::cout << sqa_stats_get_percentile(Client::stats_internal,
						      percentile)
			  << " s\n";
		(void)fflush(stdout);
	};

	printSummaryLine("mean:", sqa_stats_get_mean);
	printSummaryLine("median:", sqa_stats_get_median);
	printSummaryLine("min:", sqa_stats_get_min_as_seconds);
	printSummaryLine("max:", sqa_stats_get_max_as_seconds);
	printSummaryLine("std:", sqa_stats_get_standard_deviation);
	printSummaryLine("variance:", sqa_stats_get_variance);
	constexpr double PERCENTILE_95 = 95.0;
	constexpr double PERCENTILE_99 = 99.0;
	constexpr double PERCENTILE_99_9 = 99.9;
	printPercentileLine("p95:", PERCENTILE_95);
	printPercentileLine("p99:", PERCENTILE_99);
	printPercentileLine("p99.9:", PERCENTILE_99_9);
}

auto td_to_json(td_histogram_t *histogram) -> nlohmann::json
{
	nlohmann::json json;
	td_compress(histogram);
	json["compression"] = histogram->compression;

	// Create the digest-centroid array
	nlohmann::json centroidsJson = nlohmann::json::array();
	for (unsigned int i = 0; i < histogram->merged_nodes; ++i) {
		centroidsJson.push_back({{"m", histogram->nodes_mean[i]},
					 {"c", histogram->nodes_weight[i]}});
	}

	json["digest-centroid"] = centroidsJson;
	return json;
}

// Map TOS values to traffic classes
constexpr uint8_t TOS_BE = 0x00;
constexpr uint8_t TOS_BK = 0x20;
constexpr uint8_t TOS_VI = 0x80;
constexpr uint8_t TOS_VO = 0xA0;

auto map_tos_to_traffic_class(uint8_t tos) -> std::string
{
	switch (tos) {
	case TOS_BE:
		return "BE";
	case TOS_BK:
		return "BK";
	case TOS_VI:
		return "VI";
	case TOS_VO:
		return "VO";
	default:
		return "Unknown";
	}
}

void Client::JsonLog(const std::string &json_output_file)
{
	nlohmann::json logData;
	auto first_sent_seconds = static_cast<time_t>(
		Client::first_packet_sent_epoch_nanoseconds /
		static_cast<uint64_t>(NANOSECONDS_IN_SECOND));
	uint64_t microseconds = Client::first_packet_sent_epoch_nanoseconds %
				NANOSECONDS_IN_MICROSECOND;
	auto *now_as_tm_date = std::gmtime(&first_sent_seconds);
	constexpr size_t DATE_BUFFER_SIZE = 80;
	std::array<char, DATE_BUFFER_SIZE> first_packet_sent_date{};
	if (strftime(first_packet_sent_date.data(),
		     first_packet_sent_date.size(),
		     "%Y-%m-%dT%H:%M:%S",
		     now_as_tm_date) == 0) {
		throw std::runtime_error("Error formatting date");
	}
	// Add the microseconds back in:
	constexpr size_t FIRST_PACKET_SENT_DATE_WITH_MICROSECONDS_SIZE = 91;
	std::array<char, FIRST_PACKET_SENT_DATE_WITH_MICROSECONDS_SIZE>
		first_packet_sent_date_with_microseconds{};
	std::ostringstream oss;
	oss << first_packet_sent_date.data() << "." << std::setfill('0')
	    << std::setw(6) << microseconds << "Z";
	std::string formatted_date = oss.str();
	if (formatted_date.size() >=
	    first_packet_sent_date_with_microseconds.size()) {
		throw std::runtime_error(
			"Error formatting date with microseconds");
	}
	std::strncpy(first_packet_sent_date_with_microseconds.data(),
		     formatted_date.c_str(),
		     first_packet_sent_date_with_microseconds.size());
	uint64_t duration_nanoseconds =
		(Client::last_packet_received_epoch_nanoseconds -
		 Client::first_packet_sent_epoch_nanoseconds);
	double duration = static_cast<double>(duration_nanoseconds) /
			  static_cast<double>(NANOSECONDS_IN_SECOND);
	// Describe the sampling pattern
	nlohmann::json samplingpattern;
	samplingpattern["type"] = "Erlang-k";
	samplingpattern["mean"] =
		args.mean_inter_packet_delay_ms / MILLISECONDS_IN_SECOND;
	samplingpattern["min"] = 0;
	constexpr double MAX_SAMPLING_PATTERN_DELAY = 10.0;
	samplingpattern["max"] = MAX_SAMPLING_PATTERN_DELAY;
	logData["sampling_pattern"] = samplingpattern;
	// Describe the packet size distribution
	logData["packet_sizes"] = nlohmann::json::array();
	for (const auto &payload_len : args.payload_lens) {
		logData["packet_sizes"].push_back(payload_len);
	}
	logData["traffic_class"] = map_tos_to_traffic_class(args.snd_tos);

	// Describe the observation points
	logData["intermediate_nodes"] = nlohmann::json::array();
	logData["start_node"] = {{"ip", "localhost"},
				 {"port", args.local_port}};
	// Loop through the list of remote hosts and ports
	for (uint32_t i = 0; i < args.remote_hosts.size(); ++i) {
		logData["intermediate_nodes"].push_back(
			{{"ip", args.remote_hosts[i]},
			 {"port", args.remote_ports[i]},
			 {"label", "1"}});
		logData["intermediate_nodes"].push_back(
			{{"ip", args.remote_hosts[i]},
			 {"port", args.remote_ports[i]},
			 {"label", "2"}});
	}
	logData["end_node"] = {{"ip", "localhost"}, {"port", args.local_port}};

	logData["version"] = "0.1";
	logData["qualityattenuationaggregate"] = {
		{"t0", first_packet_sent_date_with_microseconds},
		{"duration", duration},
		{"num_samples",
		 sqa_stats_get_number_of_samples(Client::stats_RTT)},
		{"num_lost_samples",
		 sqa_stats_get_number_of_lost_packets(Client::stats_RTT)},
		{"max", sqa_stats_get_max_as_seconds(Client::stats_RTT)},
		{"min", sqa_stats_get_min_as_seconds(Client::stats_RTT)},
		{"mean", sqa_stats_get_mean(Client::stats_RTT)},
		{"variance", sqa_stats_get_variance(Client::stats_RTT)},
		{"empirical_distribution",
		 td_to_json(Client::stats_RTT->empirical_distribution)},
	};

	// Dump data to file
	std::ofstream file(json_output_file);
	file << std::setw(4) << logData << std::endl;
	file.close();
}
