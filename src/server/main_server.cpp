#include <CLI/CLI.hpp>
#include "Server.h"
#include <iostream>
#include <unistd.h>

static auto parse_args(int argc, char **argv)
{
    Args args{};
    uint8_t tos = 0;
    std::string title = "Twamp-Light implementation written by Domos. Version " + std::string(TWAMP_VERSION_TXT);
    CLI::App app{std::move(title)};
    app.option_defaults()->always_capture_default(true);
    app.add_option("-a, --local_address",
                   args.local_host,
                   "The address to set up the local socket on. Auto-selects by default.");
    app.add_option("-P, --local_port", args.local_port, "The port to set up the local socket on.");
    app.add_option("-n, --num_samples",
                   args.num_samples,
                   "Number of samples to expect before shutdown. Set to 0 to expect unlimited samples.");
    app.add_option(
           "-t, --timeout",
           args.timeout,
           "How long (in seconds) to keep the socket open, when no packets are incoming. Set to 0 to disable timeout.")
        ->default_str(std::to_string(args.timeout));
    app.add_option("--sep", args.sep, "The separator to use in the output.");
    app.add_option("--ip", args.ip_version, "The IP version to use.");
    auto *opt_tos = app.add_option("-T, --tos", tos, "The TOS value (<256).")
                        ->check(CLI::Range(256))
                        ->default_str(std::to_string(args.snd_tos));
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        std::exit((app).exit(e));
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
        Server server = Server(args);
        return server.listen();
    } catch (const CLI::BadNameString &e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::runtime_error &e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}
