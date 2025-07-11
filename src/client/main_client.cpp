//
// Created by vladim0105 on 12/15/21.
//
#include "Client.h"
#include <CLI/CLI.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#include <unistd.h>

static auto parse_args(int argc, char **argv) -> Args
{
    Args args = {};
    bool print_version = false;
    std::string title = "Twamp-Light implementation written by Domos. Version " + std::string(TWAMP_VERSION_TXT);
    CLI::App app{std::move(title)};
    app.option_defaults()->always_capture_default(true);
    app.add_option("-a, --local_address",
                   args.local_host,
                   "The address to set up the local socket on. Auto-selects by default.");
    app.add_option("-P, --local_port", args.local_port, "The port to set up the local socket on.");
    constexpr auto MIN_PAYLOAD_LEN = 42;
    constexpr auto MAX_PAYLOAD_LEN = 1473;
    app.add_option(
           "-l, --payload_lens",
           args.payload_lens,
           "The payload length. Must be in range (42, 1473). Can be multiple values, in which case the payload size for each packet will be sampled randomly from the list.")
        ->default_str(vectorToString(args.payload_lens, " "))
        ->check(CLI::Range(MIN_PAYLOAD_LEN, MAX_PAYLOAD_LEN));
    app.add_option("-n, --num_samples", args.num_samples, "Number of samples to expect. Set to 0 for unlimited.");
    app.add_option("-t, --timeout",
                   args.timeout,
                   "How long (in seconds) to wait for response on each packet before concluding the packet is lost.")
        ->default_str(std::to_string(args.timeout));
    app.add_option("-s, --seed", args.seed, "Seed for the RNG. 0 means random.");
    app.add_flag("--print-digest{true}", args.print_digest, "Prints a statistical summary at the end.");
    app.add_option("-j, --json-output", args.json_output_file, "Filename to dump json output to");
    app.add_flag("--print-lost-packets",
                 args.print_lost_packets,
                 "Prints sent and lost packet counters, legacy format only");
    app.add_option("--print-RTT-only", args.print_RTT_only, "Prints only the RTT values.");
    app.add_option("--print-format",
                   args.print_format,
                   "which format to print the output in. Can be 'legacy', 'raw', 'clockcorrected'")
        ->default_str(args.print_format);
    app.add_option("--sep", args.sep, "The separator to use in the output.");
    app.add_option("--ip", args.ip_version, "The IP version to use.");
    app.add_option(
        "--runtime",
        args.runtime,
        "Send packets for this number of seconds. The total runtime will depend on RTT, but is limited to the sum of the runtime and timeout parameters. This option overrides the -n (--num_samples) option.");
    app.add_option("-i, --mean_inter_packet_delay",
                   args.mean_inter_packet_delay_ms,
                   "The mean inter-packet delay in milliseconds.")
        ->default_str(std::to_string(args.mean_inter_packet_delay_ms));
    app.add_flag("--constant-inter-packet-delay",
                 args.constant_inter_packet_delay,
                 "The constant inter-packet delay in milliseconds. Overrides the default Poisson traffic pattern.");
    uint8_t tos = 0;
    auto *opt_tos = app.add_option("-T, --tos", tos, "The TOS value (<256).")
                        ->check(CLI::Range(256))
                        ->default_str(std::to_string(args.snd_tos));
    app.add_flag("-V{true}, --version{true}", print_version, "Print Version info");

    std::vector<std::string> ipPortStrs;
    app.add_option("addresses", ipPortStrs, "IPs and Ports in the format IP:Port")
        ->check([&args](const std::string &str) {
            std::string ip;
            uint16_t port = 0;
            if (args.ip_version == IPV6) {
                if (!parseIPv6Port(str, ip, port)) {
                    return "Address must be in the format IP:Port";
                }
            } else if (args.ip_version == IPV4) {
                if (!parseIPPort(str, ip, port)) {
                    return "Address must be in the format IP:Port";
                }
            }
            args.remote_hosts.push_back(std::move(ip));
            args.remote_ports.push_back(port);
            return "";
        });

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit((app).exit(e));
    }

    if (print_version) {
        std::cout << "Twamp Light Version " << std::string(TWAMP_VERSION_TXT) << std::endl;
        std::cout << "Commit ID " << std::string(TWAMP_GIT_COMMIT_ID);
        std::cout << ", git describe: " << std::string(TWAMP_GIT_DESCRIBE);
        std::cout << ",  submodules: qoo-c (" << std::string(QOO_GIT_DESCRIBE) << ") ";
        std::cout << "t-digest-c (" << std::string(TDIGEST_GIT_DESCRIBE) << ")" << std::endl;
        (void) fflush(stdout);
        std::exit(EXIT_SUCCESS);
    } else { // i dont't know how to override addresses options required() modifier
        if (ipPortStrs.empty()) {
            std::cout << "Address must be in the format IP:Port\n";
            (void) fflush(stdout);
            exit(EXIT_SUCCESS);
        }
    }

    if (*opt_tos) {
        args.snd_tos = tos - (((tos & 0x2) >> 1) & (tos & 0x1));
    }
    return args;
}

auto main(int argc, char **argv) -> int
{
    try {
        Args args = parse_args(argc, argv);
        if (args.runtime > 0 && args.num_samples > 0) {
            std::cout
                << "Both --runtime and --num_samples options are set. Ignoring --num_samples and using --runtime instead."
                << std::endl;
            args.num_samples = 0;
        }
        Client client = Client(args);
        std::thread receiver_thread(&Client::runReceiverThread, &client);
        std::thread sender_thread(&Client::runSenderThread, &client);
        client.printHeader();
        if (args.print_format != "legacy" || args.print_lost_packets) {
            client.runCollatorThread();
        }
        sender_thread.join();
        receiver_thread.join();
        int packets_sent = client.getSentPackets();
        if (args.print_digest && args.print_format != "legacy") {
            client.printStats(packets_sent);
        }
        if (!args.json_output_file.empty() && args.print_format != "legacy") {
            client.JsonLog(args.json_output_file);
        }
        return 0;
    } catch (const CLI::BadNameString &e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::runtime_error &e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const nlohmann::json_abi_v3_12_0::detail::type_error &e) {
        std::cerr << "JSON error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
