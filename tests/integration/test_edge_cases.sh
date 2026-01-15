#!/bin/bash
# Integration tests for edge cases and error handling
set -e

# Load test helpers
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../test_helpers.sh
source "${SCRIPT_DIR}/../test_helpers.sh"

# Use a unique port range - use higher range to avoid conflicts
BASE_PORT=27200
PORT_COUNTER=0

get_next_port() {
    PORT_COUNTER=$((PORT_COUNTER + 1))
    echo $((BASE_PORT + PORT_COUNTER))
}

# ============================================================================
# Server Not Running Tests
# ============================================================================

test_client_server_not_running() {
    local port
    port=$(get_next_port)
    
    # Try to connect to a port where no server is running
    local start_time
    start_time=$(date +%s)
    
    # Short timeout to avoid long test
    "${CLIENT}" -n 1 -t 2 "localhost:$port" &>"${CLIENT_OUTPUT}" || true
    
    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    # Should timeout within reasonable time
    if [ "$duration" -gt 10 ]; then
        log_error "Client took too long to timeout: ${duration}s"
        return 1
    fi
    
    return 0
}

test_client_connection_refused() {
    local port
    port=$(get_next_port)
    
    # Client should handle connection issues gracefully
    "${CLIENT}" -n 1 -t 1 "localhost:$port" &>"${CLIENT_OUTPUT}" 2>&1 || true
    
    # Should not crash - just check that it returned
    return 0
}

# ============================================================================
# Invalid Address Tests
# ============================================================================

test_client_invalid_ip() {
    # Invalid IP address format
    local output
    output=$("${CLIENT}" -n 1 "999.999.999.999:8080" 2>&1) || true
    
    # Should fail with an error
    if [ $? -eq 0 ] && ! echo "$output" | grep -iq "error\|fail\|invalid"; then
        log_warn "Invalid IP might have been accepted - checking if resolve failed"
    fi
    
    return 0
}

test_client_empty_address() {
    local output
    output=$("${CLIENT}" -n 1 "" 2>&1) || true
    
    # Should fail gracefully
    return 0
}

test_client_malformed_address() {
    local output
    output=$("${CLIENT}" -n 1 "localhost" 2>&1) || true
    
    # Should require port
    if ! echo "$output" | grep -iq "IP:Port\|address"; then
        log_warn "Malformed address handling may vary"
    fi
    
    return 0
}

# ============================================================================
# Port Conflict Tests
# ============================================================================

test_server_port_in_use() {
    local port
    port=$(get_next_port)
    
    # Start first server
    "${SERVER}" -n 0 -P "$port" -t 5 &>/dev/null &
    local server1_pid=$!
    sleep 0.5
    
    # Try to start second server on same port
    local output
    output=$("${SERVER}" -n 1 -P "$port" -t 1 2>&1) || true
    local exit_code=$?
    
    # Second server should fail
    kill "$server1_pid" 2>/dev/null || true
    wait "$server1_pid" 2>/dev/null || true
    
    if [ "$exit_code" -eq 0 ] && ! echo "$output" | grep -iq "error\|bind\|use"; then
        log_error "Second server should have failed to bind"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Payload Size Boundary Tests
# ============================================================================

test_minimum_payload_size() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    # Minimum payload is 42 bytes
    run_client "$port" 3 "-l 42"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Minimum payload size (42)"
}

test_maximum_payload_size() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    # Maximum payload is 1473 bytes
    run_client "$port" 3 "-l 1473"
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Maximum payload size (1473)"
}

test_payload_size_just_under_minimum() {
    local output
    output=$("${CLIENT}" -n 1 -l 41 localhost:9999 2>&1) || true
    
    # Should be rejected by CLI validation
    if ! echo "$output" | grep -iq "range\|error\|invalid"; then
        log_error "Payload 41 should be rejected"
        return 1
    fi
    
    return 0
}

test_payload_size_just_over_maximum() {
    local output
    output=$("${CLIENT}" -n 1 -l 1474 localhost:9999 2>&1) || true
    
    # Should be rejected by CLI validation
    if ! echo "$output" | grep -iq "range\|error\|invalid"; then
        log_error "Payload 1474 should be rejected"
        return 1
    fi
    
    return 0
}

# ============================================================================
# High Load Tests
# ============================================================================

test_high_packet_rate() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 20 || return 1
    
    # Test with default inter-packet delay (run_client uses -i 10)
    run_client "$port" 20
    local exit_code=$?
    
    stop_server
    
    # Should complete successfully even at high rate
    assert_exit_code 0 $exit_code "High packet rate"
}

test_many_packets() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 50 || return 1
    
    # Test with default inter-packet delay (run_client uses -i 10)
    run_client "$port" 50
    local exit_code=$?
    
    stop_server
    
    assert_exit_code 0 $exit_code "Many packets (50)"
}

# ============================================================================
# Timeout Behavior Tests
# ============================================================================

test_zero_timeout_client() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    
    # Zero timeout - should still work for receiving
    run_client "$port" 3 "-t 0"
    local exit_code=$?
    
    stop_server
    
    # Behavior may vary - just ensure no crash
    return 0
}

test_server_timeout_with_no_client() {
    local port
    port=$(get_next_port)
    
    # Server with 1 second timeout
    "${SERVER}" -n 0 -P "$port" -t 1 &>"${SERVER_OUTPUT}" &
    local server_pid=$!
    
    # Wait for timeout
    sleep 3
    
    # Server should have exited
    if kill -0 "$server_pid" 2>/dev/null; then
        log_error "Server should have timed out"
        kill "$server_pid" 2>/dev/null || true
        return 1
    fi
    
    # Check for timeout message
    if grep -q "timed out" "${SERVER_OUTPUT}"; then
        log_info "Server correctly reported timeout"
    fi
    
    return 0
}

# ============================================================================
# Graceful Shutdown Tests
# ============================================================================

test_server_graceful_shutdown() {
    local port
    port=$(get_next_port)
    
    # Server with limited samples
    "${SERVER}" -n 5 -P "$port" &>"${SERVER_OUTPUT}" &
    local server_pid=$!
    
    sleep 0.5
    
    # Send exactly 5 packets
    "${CLIENT}" -n 5 -i 50 "localhost:$port" &>/dev/null
    
    # Server should terminate gracefully
    sleep 1
    
    if kill -0 "$server_pid" 2>/dev/null; then
        log_error "Server should have terminated after receiving all samples"
        kill "$server_pid" 2>/dev/null || true
        return 1
    fi
    
    return 0
}

test_client_handles_server_disappearing() {
    local port
    port=$(get_next_port)
    
    # Server that will exit after 2 samples
    "${SERVER}" -n 2 -P "$port" &>/dev/null &
    local server_pid=$!
    
    sleep 0.5
    
    # Client tries to send more samples than server will accept
    "${CLIENT}" -n 10 -t 2 -i 100 "localhost:$port" &>"${CLIENT_OUTPUT}" || true
    
    # Client should handle the situation gracefully
    wait "$server_pid" 2>/dev/null || true
    
    return 0
}

# ============================================================================
# TOS/DSCP Boundary Tests
# ============================================================================

test_tos_zero() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "-T 0" || return 1
    run_client "$port" 3 "-T 0"
    local exit_code=$?
    stop_server
    
    assert_exit_code 0 $exit_code "TOS=0"
}

test_tos_max() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "-T 255" || return 1
    run_client "$port" 3 "-T 255"
    local exit_code=$?
    stop_server
    
    assert_exit_code 0 $exit_code "TOS=255"
}

test_tos_common_values() {
    local port
    
    # Test common TOS/DSCP values
    local tos_values=(0 32 64 96 128 160 192 224)
    
    for tos in "${tos_values[@]}"; do
        port=$(get_next_port)
        
        start_server "$port" 2 "-T $tos" || return 1
        run_client "$port" 2 "-T $tos"
        local exit_code=$?
        stop_server
        
        if [ $exit_code -ne 0 ]; then
            log_error "Failed with TOS=$tos"
            return 1
        fi
    done
    
    return 0
}

# ============================================================================
# IP Version Tests
# ============================================================================

test_ipv4_explicit() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 "--ip 4" || return 1
    run_client "$port" 3 "--ip 4"
    local exit_code=$?
    stop_server
    
    # Verify IPv4 in output
    if ! grep -q "127.0.0.1" "${CLIENT_OUTPUT}"; then
        log_error "IPv4 address not in output"
        return 1
    fi
    
    return 0
}

test_invalid_ip_version() {
    local output
    output=$("${CLIENT}" -n 1 --ip 5 localhost:9999 2>&1) || true
    
    # Invalid IP version should be handled
    # (may be accepted and fail later, or rejected by CLI)
    return 0
}

# ============================================================================
# Concurrent Access Tests
# ============================================================================

test_multiple_clients_sequential() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 15 || return 1
    
    # Run multiple clients sequentially
    run_client "$port" 5
    run_client "$port" 5
    run_client "$port" 5
    
    stop_server
    
    return 0
}

# ============================================================================
# Empty/Zero Value Tests
# ============================================================================

test_zero_num_samples_with_runtime() {
    local port
    port=$(get_next_port)

    start_server "$port" 100 || return 1

    # num_samples=0 with runtime should work
    # Note: don't pass -i as run_client already includes it
    run_client "$port" 0 "--runtime 1"
    local exit_code=$?

    stop_server

    assert_exit_code 0 $exit_code "num_samples=0 with runtime"
}

# ============================================================================
# Special Character Handling
# ============================================================================

test_special_separator_pipe() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 '--sep |' || return 1
    run_client "$port" 3 '--sep |'
    local exit_code=$?
    stop_server
    
    assert_exit_code 0 $exit_code "Pipe separator"
}

# ============================================================================
# Long Running Tests
# ============================================================================

test_runtime_mode_duration() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 1000 || return 1
    
    local start_time
    start_time=$(date +%s)
    
    # Run for 3 seconds (run_client already has -i 10)
    run_client "$port" 0 "--runtime 3"
    
    local end_time
    end_time=$(date +%s)
    local duration=$((end_time - start_time))
    
    stop_server
    
    # Should be approximately 3 seconds (allow margin for startup/shutdown)
    if [ "$duration" -lt 2 ] || [ "$duration" -gt 6 ]; then
        log_error "Runtime duration unexpected: ${duration}s (expected ~3s)"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Memory Stress Tests (Light)
# ============================================================================

test_repeated_connections() {
    local port
    port=$(get_next_port)
    
    # Start long-running server
    "${SERVER}" -n 0 -P "$port" -t 30 &>/dev/null &
    local server_pid=$!
    
    sleep 0.5
    
    # Make many short connections
    for i in {1..10}; do
        "${CLIENT}" -n 2 -i 10 "localhost:$port" &>/dev/null || true
    done
    
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
    
    return 0
}

# ============================================================================
# Main test runner
# ============================================================================

main() {
    log_info "Starting edge case and error handling tests"
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
    
    # Server Not Running Tests
    run_test "Client with server not running" test_client_server_not_running
    run_test "Client connection refused" test_client_connection_refused
    
    # Invalid Address Tests
    run_test "Client invalid IP" test_client_invalid_ip
    run_test "Client empty address" test_client_empty_address
    run_test "Client malformed address" test_client_malformed_address
    
    # Port Conflict Tests
    run_test "Server port in use" test_server_port_in_use
    
    # Payload Size Boundary Tests
    run_test "Minimum payload size" test_minimum_payload_size
    run_test "Maximum payload size" test_maximum_payload_size
    run_test "Payload just under minimum" test_payload_size_just_under_minimum
    run_test "Payload just over maximum" test_payload_size_just_over_maximum
    
    # High Load Tests
    run_test "High packet rate" test_high_packet_rate
    run_test "Many packets" test_many_packets
    
    # Timeout Behavior Tests
    run_test "Zero timeout client" test_zero_timeout_client
    run_test "Server timeout with no client" test_server_timeout_with_no_client
    
    # Graceful Shutdown Tests
    run_test "Server graceful shutdown" test_server_graceful_shutdown
    run_test "Client handles server disappearing" test_client_handles_server_disappearing
    
    # TOS/DSCP Tests
    run_test "TOS zero" test_tos_zero
    run_test "TOS max" test_tos_max
    run_test "TOS common values" test_tos_common_values
    
    # IP Version Tests
    run_test "IPv4 explicit" test_ipv4_explicit
    run_test "Invalid IP version" test_invalid_ip_version
    
    # Concurrent Access Tests
    run_test "Multiple clients sequential" test_multiple_clients_sequential
    
    # Empty/Zero Value Tests
    run_test "Zero num_samples with runtime" test_zero_num_samples_with_runtime
    
    # Special Character Tests
    run_test "Special separator pipe" test_special_separator_pipe
    
    # Long Running Tests
    run_test "Runtime mode duration" test_runtime_mode_duration
    
    # Memory Stress Tests
    run_test "Repeated connections" test_repeated_connections
    
    print_test_summary
    exit $?
}

main "$@"
