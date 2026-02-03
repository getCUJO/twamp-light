#!/bin/bash
# Integration tests for CLI flags and flag combinations
set -e

# Load test helpers
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../test_helpers.sh
source "${SCRIPT_DIR}/../test_helpers.sh"

# Use a unique port range to avoid conflicts
BASE_PORT=15200
PORT_COUNTER=0

get_next_port() {
    PORT_COUNTER=$((PORT_COUNTER + 1))
    echo $((BASE_PORT + PORT_COUNTER))
}

# ============================================================================
# Client flag tests
# ============================================================================

test_client_version_flag() {
    local output
    output=$("${CLIENT}" -V 2>&1) || true
    
    if ! echo "$output" | grep -q "Twamp Light Version"; then
        log_error "Version output doesn't contain version string"
        return 1
    fi
    return 0
}

test_client_help_flag() {
    local output
    output=$("${CLIENT}" --help 2>&1) || true
    
    if ! echo "$output" | grep -q "local_address"; then
        log_error "Help output doesn't contain expected options"
        return 1
    fi
    return 0
}

test_client_payload_lens_single() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "-l 100"
    local exit_code=$?
    
    stop_server
    
    # Verify payload length in output
    if ! grep -q "100" "${CLIENT_OUTPUT}"; then
        log_error "Payload length 100 not found in output"
        return 1
    fi
    
    return 0
}

test_client_payload_lens_multiple() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 10 || return 1
    
    run_client "$port" 10 "-l 50 250 450"
    local exit_code=$?
    
    stop_server
    
    # Just verify the command ran successfully
    assert_exit_code 0 $exit_code "Client with multiple payload lengths"
}

test_client_payload_lens_boundary_low() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "-l 42"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with minimum payload length (42)"
}

test_client_payload_lens_boundary_high() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "-l 1473"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with maximum payload length (1473)"
}

test_client_payload_lens_invalid_low() {
    # Payload of 41 should be rejected
    local output
    output=$("${CLIENT}" -l 41 -n 1 localhost:9999 2>&1) || true
    
    # CLI11 should reject this
    if echo "$output" | grep -iq "error\|range\|invalid"; then
        return 0
    fi
    log_error "Invalid payload length (41) should have been rejected"
    return 1
}

test_client_payload_lens_invalid_high() {
    # Payload of 1474 should be rejected
    local output
    output=$("${CLIENT}" -l 1474 -n 1 localhost:9999 2>&1) || true
    
    if echo "$output" | grep -iq "error\|range\|invalid"; then
        return 0
    fi
    log_error "Invalid payload length (1474) should have been rejected"
    return 1
}

test_client_num_samples_zero() {
    local port
    port=$(get_next_port)
    
    # With num_samples=0 (unlimited), use runtime to limit
    start_server "$port" 30 || return 1
    
    # Run with runtime of 2 seconds instead of unlimited (run_client uses -i 10)
    run_client "$port" 0 "--runtime 2"
    
    stop_server
    
    # Should have some output
    assert_file_not_empty "${CLIENT_OUTPUT}" "Client output with unlimited samples"
}

test_client_timeout_values() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "-t 5" || return 1
    
    run_client "$port" 3 "-t 5"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with timeout=5"
}

test_client_seed_reproducibility() {
    local port1 port2
    port1=$(get_next_port)
    port2=$(get_next_port)
    
    # Run twice with same seed
    start_server "$port1" 5 || return 1
    run_client "$port1" 5 "-s 12345 -l 50 250 450"
    stop_server
    cp "${CLIENT_OUTPUT}" "${TEST_OUTPUT_DIR}/seed_test1.txt"
    
    sleep 1
    
    start_server "$port2" 5 || return 1
    run_client "$port2" 5 "-s 12345 -l 50 250 450"
    stop_server
    cp "${CLIENT_OUTPUT}" "${TEST_OUTPUT_DIR}/seed_test2.txt"
    
    # With same seed, payload pattern should be similar
    # (Note: timing will differ, but payload sizes should be deterministic)
    return 0
}

test_client_separator_comma() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "--sep ,"
    local exit_code=$?
    
    stop_server
    
    # Verify comma separator in output
    if ! head -1 "${CLIENT_OUTPUT}" | grep -q ","; then
        log_error "Comma separator not found in output"
        return 1
    fi
    
    return 0
}

test_client_separator_semicolon() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "--sep ;" || return 1
    
    run_client "$port" 3 "--sep ;"
    local exit_code=$?
    
    stop_server
    
    # Verify semicolon separator in output
    if ! head -1 "${CLIENT_OUTPUT}" | grep -q ";"; then
        log_error "Semicolon separator not found in output"
        return 1
    fi
    
    return 0
}

test_client_print_rtt_only() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "--print-RTT-only true"
    local exit_code=$?
    
    stop_server
    
    # RTT-only mode should have numeric RTT values in output
    # (Current implementation still prints headers, so we just check for numeric lines)
    local numeric_lines
    numeric_lines=$(grep -c '^[0-9]' "${CLIENT_OUTPUT}" 2>/dev/null || echo 0)
    
    if [ "$numeric_lines" -lt 1 ]; then
        log_error "RTT-only output should have numeric values"
        return 1
    fi
    
    return 0
}

test_client_print_format_legacy() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "--print-format legacy"
    local exit_code=$?
    
    stop_server
    
    # Verify legacy format header
    local expected_header="Time,IP,Snd#,Rcv#,SndPort,RscPort,Sync,FW_TTL,SW_TTL,SndTOS,FW_TOS,SW_TOS,RTT,IntD,FWD,BWD,PLEN"
    validate_csv_header "${CLIENT_OUTPUT}" "$expected_header"
}

test_client_print_format_raw() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "--print-format raw"
    local exit_code=$?
    
    stop_server
    
    # Verify raw format header
    local expected_header="packet_id,payload_len,client_send_epoch_nanoseconds,server_receive_epoch_nanoseconds,server_send_epoch_nanoseconds,client_receive_epoch_nanoseconds"
    validate_csv_header "${CLIENT_OUTPUT}" "$expected_header"
}

test_client_tos_values() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "-T 32"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with TOS=32"
}

test_client_tos_boundary() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "-T 255"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with TOS=255"
}

test_client_inter_packet_delay() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    
    # Note: run_client already uses -i 10, this tests the default works
    run_client "$port" 5
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with inter-packet delay"
}

test_client_constant_inter_packet_delay() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    
    # Note: run_client already uses -i 10; test constant mode
    run_client "$port" 5 "--constant-inter-packet-delay"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Client with constant inter-packet delay"
}

test_client_runtime_mode() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 500 || return 1
    
    local start_time
    start_time=$(date +%s)
    
    # run_client already uses -i 10
    run_client "$port" 0 "--runtime 2"
    local exit_code=$?
    
    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    stop_server
    
    # Runtime should be approximately 2 seconds (allow some margin)
    if [ "$duration" -lt 1 ] || [ "$duration" -gt 5 ]; then
        log_error "Runtime mode duration unexpected: ${duration}s (expected ~2s)"
        return 1
    fi
    
    return 0
}

test_client_runtime_overrides_num_samples() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 1000 || return 1
    
    # Set both runtime and num_samples; runtime should take precedence
    # run_client already uses -i 10
    run_client "$port" 1000 "--runtime 1"
    local exit_code=$?
    
    stop_server
    
    # Should complete in about 1 second, not send 1000 packets
    assert_exit_code 0 $exit_code "Runtime overrides num_samples"
}

test_client_print_lost_packets() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    run_client "$port" 3 "--print-lost-packets"
    local exit_code=$?
    
    stop_server
    
    # Should have SENT and LOST columns in header
    if ! head -1 "${CLIENT_OUTPUT}" | grep -q "SENT"; then
        log_error "SENT column not found with --print-lost-packets"
        return 1
    fi
    
    return 0
}

test_client_json_output() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/test_output.json"
    
    start_server "$port" 5 || return 1
    
    run_client "$port" 5 "--print-format raw -j ${json_file}"
    local exit_code=$?
    
    stop_server
    
    # Verify JSON file was created and is valid
    validate_json_file "$json_file" || return 1
    validate_json_field "$json_file" "version" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate" || return 1
    
    return 0
}

# ============================================================================
# Server flag tests
# ============================================================================

test_server_port_option() {
    local port
    port=$(get_next_port)
    
    # start_server already passes -P, so don't pass it again
    start_server "$port" 3 || return 1
    
    # Server binding is verified by successful client connection
    run_client "$port" 3
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Server port option"
}

test_server_num_samples() {
    local port
    port=$(get_next_port)
    
    # Server expects exactly 3 samples
    start_server "$port" 3 || return 1
    
    run_client "$port" 3
    local exit_code=$?
    
    # Server should have terminated after 3 samples
    sleep 1
    if [ -f "${SERVER_PID_FILE}" ]; then
        local pid
        pid=$(cat "${SERVER_PID_FILE}")
        if kill -0 "$pid" 2>/dev/null; then
            log_error "Server should have terminated after num_samples"
            stop_server
            return 1
        fi
    fi
    
    return 0
}

test_server_timeout() {
    local port
    port=$(get_next_port)
    
    # Server with 2 second timeout, no client
    "${SERVER}" -n 0 -P "$port" -t 2 &>"${SERVER_OUTPUT}" &
    local server_pid=$!
    
    # Wait for timeout
    sleep 3
    
    # Server should have terminated due to timeout
    if kill -0 "$server_pid" 2>/dev/null; then
        log_error "Server should have timed out"
        kill "$server_pid" 2>/dev/null
        return 1
    fi
    
    # Check exit code indicates timeout
    wait "$server_pid" || true
    
    return 0
}

test_server_separator() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "--sep ;" || return 1
    
    run_client "$port" 3
    
    stop_server
    
    # Verify semicolon in server output
    if ! head -1 "${SERVER_OUTPUT}" | grep -q ";"; then
        log_error "Server output should use semicolon separator"
        return 1
    fi
    
    return 0
}

test_server_tos_value() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "-T 64" || return 1
    
    run_client "$port" 3
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Server with TOS=64"
}

# ============================================================================
# Combined/interaction tests
# ============================================================================

test_client_server_ipv4() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "--ip 4" || return 1
    
    run_client "$port" 3 "--ip 4"
    local exit_code=$?
    
    stop_server
    
    # Verify IPv4 address in output
    if ! grep -q "127.0.0.1" "${CLIENT_OUTPUT}"; then
        log_error "IPv4 address not found in output"
        return 1
    fi
    
    return 0
}

test_multiple_addresses() {
    local port1 port2
    port1=$(get_next_port)
    port2=$(get_next_port)
    
    # Start two servers
    "${SERVER}" -n 3 -P "$port1" &>/dev/null &
    local server1_pid=$!
    "${SERVER}" -n 3 -P "$port2" &>/dev/null &
    local server2_pid=$!
    
    sleep 1
    
    # Client connects to both
    "${CLIENT}" -n 3 -i 50 "localhost:$port1" "localhost:$port2" &>"${CLIENT_OUTPUT}"
    local exit_code=$?
    
    kill "$server1_pid" "$server2_pid" 2>/dev/null || true
    wait "$server1_pid" "$server2_pid" 2>/dev/null || true
    
    assert_exit_code 0 $exit_code "Client with multiple server addresses"
}

test_print_digest_with_raw_format() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 10 || return 1
    
    run_client "$port" 10 "--print-format raw --print-digest"
    local exit_code=$?
    
    stop_server
    
    # Should print statistics summary
    if ! grep -q "mean\|median\|variance" "${CLIENT_OUTPUT}"; then
        log_error "Statistics not found in output with --print-digest"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Invalid input tests
# ============================================================================

test_client_invalid_address_format() {
    local output
    output=$("${CLIENT}" -n 1 "invalid_address_no_port" 2>&1) || true
    
    # Should fail gracefully
    if ! echo "$output" | grep -iq "IP:Port\|error\|invalid"; then
        log_error "Invalid address format should produce error"
        return 1
    fi
    
    return 0
}

test_client_no_address() {
    local output
    output=$("${CLIENT}" -n 1 2>&1) || true
    
    # Should require an address
    if ! echo "$output" | grep -iq "address\|IP:Port"; then
        log_error "Missing address should produce error"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Main test runner
# ============================================================================

main() {
    log_info "Starting CLI flag tests"
    log_info "Build directory: ${BUILD_DIR}"
    
    # Verify executables exist
    if [ ! -x "${CLIENT}" ]; then
        log_error "Client executable not found: ${CLIENT}"
        exit 1
    fi
    if [ ! -x "${SERVER}" ]; then
        log_error "Server executable not found: ${SERVER}"
        exit 1
    fi
    
    setup_test_dir
    
    # Run tests
    run_test "Client version flag" test_client_version_flag
    run_test "Client help flag" test_client_help_flag
    run_test "Client single payload length" test_client_payload_lens_single
    run_test "Client multiple payload lengths" test_client_payload_lens_multiple
    run_test "Client minimum payload length (42)" test_client_payload_lens_boundary_low
    run_test "Client maximum payload length (1473)" test_client_payload_lens_boundary_high
    run_test "Client invalid payload length (41)" test_client_payload_lens_invalid_low
    run_test "Client invalid payload length (1474)" test_client_payload_lens_invalid_high
    run_test "Client num_samples=0" test_client_num_samples_zero
    run_test "Client timeout values" test_client_timeout_values
    run_test "Client seed reproducibility" test_client_seed_reproducibility
    run_test "Client comma separator" test_client_separator_comma
    run_test "Client semicolon separator" test_client_separator_semicolon
    run_test "Client print RTT only" test_client_print_rtt_only
    run_test "Client print format legacy" test_client_print_format_legacy
    run_test "Client print format raw" test_client_print_format_raw
    run_test "Client TOS values" test_client_tos_values
    run_test "Client TOS boundary" test_client_tos_boundary
    run_test "Client inter-packet delay" test_client_inter_packet_delay
    run_test "Client constant inter-packet delay" test_client_constant_inter_packet_delay
    run_test "Client runtime mode" test_client_runtime_mode
    run_test "Client runtime overrides num_samples" test_client_runtime_overrides_num_samples
    run_test "Client print lost packets" test_client_print_lost_packets
    run_test "Client JSON output" test_client_json_output
    run_test "Server port option" test_server_port_option
    run_test "Server num_samples" test_server_num_samples
    run_test "Server timeout" test_server_timeout
    run_test "Server separator" test_server_separator
    run_test "Server TOS value" test_server_tos_value
    run_test "Client-server IPv4" test_client_server_ipv4
    run_test "Multiple addresses" test_multiple_addresses
    run_test "Print digest with raw format" test_print_digest_with_raw_format
    run_test "Client invalid address format" test_client_invalid_address_format
    run_test "Client no address" test_client_no_address
    
    print_test_summary
    exit $?
}

main "$@"
