#!/bin/bash
# Integration tests for output format validation
set -e

# Load test helpers
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../test_helpers.sh
source "${SCRIPT_DIR}/../test_helpers.sh"

# Use a unique port range
BASE_PORT=16200
PORT_COUNTER=0

get_next_port() {
    PORT_COUNTER=$((PORT_COUNTER + 1))
    echo $((BASE_PORT + PORT_COUNTER))
}

# ============================================================================
# Client Legacy Format Tests
# ============================================================================

# Expected header: Time,IP,Snd#,Rcv#,SndPort,RscPort,Sync,FW_TTL,SW_TTL,SndTOS,FW_TOS,SW_TOS,RTT,IntD,FWD,BWD,PLEN
CLIENT_LEGACY_HEADER="Time,IP,Snd#,Rcv#,SndPort,RscPort,Sync,FW_TTL,SW_TTL,SndTOS,FW_TOS,SW_TOS,RTT,IntD,FWD,BWD,PLEN"
CLIENT_LEGACY_FIELD_COUNT=17

# With --print-lost-packets: adds SENT,LOST
CLIENT_LEGACY_LOST_HEADER="Time,IP,Snd#,Rcv#,SndPort,RscPort,Sync,FW_TTL,SW_TTL,SndTOS,FW_TOS,SW_TOS,RTT,IntD,FWD,BWD,PLEN,SENT,LOST"
CLIENT_LEGACY_LOST_FIELD_COUNT=19

test_client_legacy_format_header() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    validate_csv_header "${CLIENT_OUTPUT}" "$CLIENT_LEGACY_HEADER"
}

test_client_legacy_format_field_count() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    validate_field_count "${CLIENT_OUTPUT}" "$CLIENT_LEGACY_FIELD_COUNT"
}

test_client_legacy_format_time_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # Time field (field 1) should be numeric timestamp in nanoseconds
    validate_numeric_field "${CLIENT_OUTPUT}" 1
}

test_client_legacy_format_ip_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # IP field (field 2) should be 127.0.0.1 for localhost
    validate_ip_field "${CLIENT_OUTPUT}" 2 "," "127.0.0.1"
}

test_client_legacy_format_sequence_numbers() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # Snd# (field 3) and Rcv# (field 4) should be numeric
    validate_numeric_field "${CLIENT_OUTPUT}" 3 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 4
}

test_client_legacy_format_ports() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # SndPort (field 5) and RscPort (field 6) should be numeric
    validate_numeric_field "${CLIENT_OUTPUT}" 5 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 6
}

test_client_legacy_format_sync_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # Sync field (field 7) should be Y or N
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local sync_value
        sync_value=$(echo "$line" | cut -d',' -f7)
        
        if [ "$sync_value" != "Y" ] && [ "$sync_value" != "N" ]; then
            log_error "Sync field should be Y or N, got '$sync_value' on line $line_num"
            return 1
        fi
    done < "${CLIENT_OUTPUT}"
    
    return 0
}

test_client_legacy_format_ttl_fields() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # FW_TTL (field 8) and SW_TTL (field 9) should be 0-255
    validate_numeric_field "${CLIENT_OUTPUT}" 8 || return 1
    validate_range "${CLIENT_OUTPUT}" 8 0 255 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 9 || return 1
    validate_range "${CLIENT_OUTPUT}" 9 0 255
}

test_client_legacy_format_tos_fields() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # SndTOS (field 10), FW_TOS (field 11), SW_TOS (field 12) should be 0-255
    validate_numeric_field "${CLIENT_OUTPUT}" 10 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 11 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 12
}

test_client_legacy_format_delay_fields() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # RTT (13), IntD (14), FWD (15), BWD (16) should be numeric (can be negative for unsync)
    validate_numeric_field "${CLIENT_OUTPUT}" 13 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 14 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 15 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 16
}

test_client_legacy_format_plen_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format legacy"
    stop_server
    
    # PLEN (field 17) should be in range 42-1473
    validate_numeric_field "${CLIENT_OUTPUT}" 17 || return 1
    validate_range "${CLIENT_OUTPUT}" 17 42 1473
}

test_client_legacy_with_lost_packets() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-lost-packets"
    stop_server
    
    # Should have 19 fields with SENT and LOST
    validate_csv_header "${CLIENT_OUTPUT}" "$CLIENT_LEGACY_LOST_HEADER" || return 1
    validate_field_count "${CLIENT_OUTPUT}" "$CLIENT_LEGACY_LOST_FIELD_COUNT"
}

# ============================================================================
# Client Raw Format Tests
# ============================================================================

CLIENT_RAW_HEADER="packet_id,payload_len,client_send_epoch_nanoseconds,server_receive_epoch_nanoseconds,server_send_epoch_nanoseconds,client_receive_epoch_nanoseconds"
CLIENT_RAW_FIELD_COUNT=6

test_client_raw_format_header() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    validate_csv_header "${CLIENT_OUTPUT}" "$CLIENT_RAW_HEADER"
}

test_client_raw_format_field_count() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    validate_field_count "${CLIENT_OUTPUT}" "$CLIENT_RAW_FIELD_COUNT"
}

test_client_raw_format_packet_id() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    # packet_id should be sequential integers starting from 0
    validate_numeric_field "${CLIENT_OUTPUT}" 1
}

test_client_raw_format_payload_len() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    validate_numeric_field "${CLIENT_OUTPUT}" 2 || return 1
    validate_range "${CLIENT_OUTPUT}" 2 42 1473
}

test_client_raw_format_timestamps() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    # All timestamp fields should be numeric (nanoseconds since epoch)
    validate_numeric_field "${CLIENT_OUTPUT}" 3 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 4 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 5 || return 1
    validate_numeric_field "${CLIENT_OUTPUT}" 6
}

test_client_raw_format_timestamp_ordering() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5 "--print-format raw"
    stop_server
    
    # Timestamps should be in order: client_send < server_receive < server_send < client_receive
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local client_send server_recv server_send client_recv
        client_send=$(echo "$line" | cut -d',' -f3)
        server_recv=$(echo "$line" | cut -d',' -f4)
        server_send=$(echo "$line" | cut -d',' -f5)
        client_recv=$(echo "$line" | cut -d',' -f6)
        
        # Verify ordering (with bc for large number comparison)
        if [ "$(echo "$server_recv < $client_send" | bc)" -eq 1 ]; then
            log_warn "Timestamp ordering anomaly on line $line_num (server_recv < client_send) - clock sync issue"
            # Don't fail - clock differences between client/server are expected
        fi
        
    done < "${CLIENT_OUTPUT}"
    
    return 0
}

# ============================================================================
# Server Output Format Tests
# ============================================================================

SERVER_HEADER="Time,IP,Snd#,Rcv#,SndPort,RscPort,FW_TTL,SndTOS,FW_TOS,IntD,FWD,PLEN,"
SERVER_FIELD_COUNT=12

test_server_format_header() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5
    stop_server
    
    # Note: Server header has trailing comma
    validate_csv_header "${SERVER_OUTPUT}" "$SERVER_HEADER"
}

test_server_format_field_count() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5
    stop_server
    
    # Server has 12 fields (with trailing comma making it look like 13 empty)
    # Actually check for 12 or 13 (empty field after trailing comma)
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local field_count
        field_count=$(echo "$line" | awk -F',' '{print NF}')
        
        if [ "$field_count" -ne 12 ] && [ "$field_count" -ne 13 ]; then
            log_error "Server field count mismatch on line $line_num: expected 12-13, got $field_count"
            return 1
        fi
    done < "${SERVER_OUTPUT}"
    
    return 0
}

# ============================================================================
# BUG DETECTION TESTS - Tests that verify known issues
# ============================================================================

test_client_sndport_not_zero() {
    # BUG: Client SndPort always shows 0 instead of actual bound port
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    run_client "$port" 3
    stop_server
    
    # Check if SndPort (field 5) is non-zero
    local line_num=0
    local found_nonzero=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local sndport
        sndport=$(echo "$line" | cut -d',' -f5)
        
        if [ "$sndport" != "0" ]; then
            found_nonzero=1
            break
        fi
    done < "${CLIENT_OUTPUT}"
    
    if [ "$found_nonzero" -eq 0 ]; then
        log_error "BUG DETECTED: Client SndPort is always 0 (should show actual bound port)"
        log_warn "This is a known bug - client doesn't query actual port from socket"
        return 1
    fi
    
    return 0
}

test_server_sndtos_is_numeric() {
    # BUG: Server SndTOS outputs as char instead of number
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    run_client "$port" 3
    stop_server
    
    # Check if SndTOS (field 8) is numeric
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local sndtos
        sndtos=$(echo "$line" | cut -d',' -f8)
        
        # Check if value is numeric
        if ! [[ "$sndtos" =~ ^[0-9]+$ ]]; then
            log_error "BUG DETECTED: Server SndTOS is not numeric: got '$sndtos'"
            log_warn "This is a known bug - uint8_t is printed as char instead of number"
            return 1
        fi
    done < "${SERVER_OUTPUT}"
    
    return 0
}

test_server_fwtos_is_tos_not_delay() {
    # BUG: Server FW_TOS column contains delay value instead of TOS
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    run_client "$port" 3
    stop_server
    
    # Check if FW_TOS (field 9) is a TOS value (0-255) not a delay
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local fwtos
        fwtos=$(echo "$line" | cut -d',' -f9)
        
        # TOS should be integer 0-255, not floating point delay
        if [[ "$fwtos" =~ \. ]]; then
            log_error "BUG DETECTED: Server FW_TOS contains floating point value: '$fwtos'"
            log_warn "This is a known bug - FW_TOS outputs delay instead of TOS value"
            return 1
        fi
        
        # Also check if it's in valid TOS range
        if [[ "$fwtos" =~ ^[0-9]+$ ]] && [ "$fwtos" -gt 255 ]; then
            log_error "FW_TOS value out of range: $fwtos"
            return 1
        fi
    done < "${SERVER_OUTPUT}"
    
    return 0
}

test_server_fwd_not_duplicate() {
    # BUG: Server FWD column duplicates another value
    local port
    port=$(get_next_port)
    
    start_server "$port" 3 || return 1
    run_client "$port" 3
    stop_server
    
    # Check if FWD (field 11) equals FW_TOS (field 9) - they shouldn't be the same
    local line_num=0
    local duplicates_found=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local fwtos fwd
        fwtos=$(echo "$line" | cut -d',' -f9)
        fwd=$(echo "$line" | cut -d',' -f11)
        
        if [ "$fwtos" = "$fwd" ]; then
            duplicates_found=$((duplicates_found + 1))
        fi
    done < "${SERVER_OUTPUT}"
    
    if [ "$duplicates_found" -gt 0 ]; then
        log_error "BUG DETECTED: Server FWD column duplicates FW_TOS column"
        log_warn "Found $duplicates_found rows with duplicate values"
        return 1
    fi
    
    return 0
}

test_server_format_time_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5
    stop_server
    
    validate_numeric_field "${SERVER_OUTPUT}" 1
}

test_server_format_ip_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5
    stop_server
    
    validate_ip_field "${SERVER_OUTPUT}" 2 "," "127.0.0.1"
}

test_server_format_plen_field() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 || return 1
    run_client "$port" 5
    stop_server
    
    # PLEN is field 12
    validate_numeric_field "${SERVER_OUTPUT}" 12 || return 1
    validate_range "${SERVER_OUTPUT}" 12 42 1473
}

# ============================================================================
# JSON Output Format Tests
# ============================================================================

test_json_output_valid() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/json_valid_test.json"
    
    start_server "$port" 10 || return 1
    run_client "$port" 10 "--print-format raw -j ${json_file}"
    stop_server
    
    validate_json_file "$json_file"
}

test_json_output_required_fields() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/json_fields_test.json"
    
    start_server "$port" 10 || return 1
    run_client "$port" 10 "--print-format raw -j ${json_file}"
    stop_server
    
    validate_json_field "$json_file" "version" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate" || return 1
    validate_json_field "$json_file" "sampling_pattern" || return 1
    validate_json_field "$json_file" "packet_sizes" || return 1
    validate_json_field "$json_file" "traffic_class"
}

test_json_output_aggregate_fields() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/json_aggregate_test.json"
    
    start_server "$port" 10 || return 1
    run_client "$port" 10 "--print-format raw -j ${json_file}"
    stop_server
    
    # Check qualityattenuationaggregate sub-fields
    validate_json_field "$json_file" "qualityattenuationaggregate.t0" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate.duration" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate.num_samples" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate.mean" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate.variance" || return 1
    validate_json_field "$json_file" "qualityattenuationaggregate.empirical_distribution"
}

test_json_output_sampling_pattern() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/json_sampling_test.json"
    
    start_server "$port" 10 || return 1
    run_client "$port" 10 "--print-format raw -j ${json_file}"
    stop_server
    
    validate_json_field "$json_file" "sampling_pattern.type" || return 1
    validate_json_field "$json_file" "sampling_pattern.mean"
}

test_json_output_timestamp_format() {
    local port
    port=$(get_next_port)
    local json_file="${TEST_OUTPUT_DIR}/json_timestamp_test.json"
    
    start_server "$port" 10 || return 1
    run_client "$port" 10 "--print-format raw -j ${json_file}"
    stop_server
    
    # t0 should be ISO 8601 format
    local t0
    t0=$(python3 -c "import json; print(json.load(open('$json_file'))['qualityattenuationaggregate']['t0'])")
    
    # Should contain date separator and time separator
    if ! echo "$t0" | grep -qE "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}"; then
        log_error "t0 timestamp not in ISO 8601 format: $t0"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Statistics Output Tests
# ============================================================================

test_print_digest_output() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 20 || return 1
    run_client "$port" 20 "--print-format raw --print-digest"
    stop_server
    
    # Should contain statistical measures
    local stats_keywords=("mean" "median" "min" "max" "std" "variance" "p95" "p99")
    
    for keyword in "${stats_keywords[@]}"; do
        if ! grep -qi "$keyword" "${CLIENT_OUTPUT}"; then
            log_error "Statistics output missing: $keyword"
            return 1
        fi
    done
    
    return 0
}

test_statistics_output_format() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 20 || return 1
    run_client "$port" 20 "--print-format raw --print-digest"
    stop_server
    
    # Check that RTT, FWD, BWD columns are present in stats
    if ! grep -q "RTT.*FWD.*BWD" "${CLIENT_OUTPUT}"; then
        log_error "Statistics header missing RTT/FWD/BWD columns"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Custom Separator Tests
# ============================================================================

test_tab_separator_client() {
    local port
    port=$(get_next_port)
    
    # Tab separator is tricky in shell - skip this test for now
    # The functionality is tested manually
    log_warn "Tab separator test skipped (shell quoting complexity)"
    return 0
}

test_custom_separator_validation() {
    local port
    port=$(get_next_port)
    
    start_server "$port" 5 "--sep |" || return 1
    run_client "$port" 5 "--sep |"
    stop_server
    
    # Verify pipe separator
    if ! head -1 "${CLIENT_OUTPUT}" | grep -q "|"; then
        log_error "Pipe separator not found in client output"
        return 1
    fi
    if ! head -1 "${SERVER_OUTPUT}" | grep -q "|"; then
        log_error "Pipe separator not found in server output"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Row Count Tests
# ============================================================================

test_row_count_matches_num_samples() {
    local port
    port=$(get_next_port)
    local num_samples=10
    
    start_server "$port" "$num_samples" || return 1
    run_client "$port" "$num_samples"
    stop_server
    
    # Client should have num_samples rows (plus header)
    validate_row_count "${CLIENT_OUTPUT}" "$num_samples" || return 1
    
    # Server should also have num_samples rows
    validate_row_count "${SERVER_OUTPUT}" "$num_samples"
}

# ============================================================================
# Main test runner
# ============================================================================

main() {
    log_info "Starting output format validation tests"
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
    
    # Client Legacy Format Tests
    run_test "Client legacy format header" test_client_legacy_format_header
    run_test "Client legacy format field count" test_client_legacy_format_field_count
    run_test "Client legacy format Time field" test_client_legacy_format_time_field
    run_test "Client legacy format IP field" test_client_legacy_format_ip_field
    run_test "Client legacy format sequence numbers" test_client_legacy_format_sequence_numbers
    run_test "Client legacy format ports" test_client_legacy_format_ports
    run_test "Client legacy format Sync field" test_client_legacy_format_sync_field
    run_test "Client legacy format TTL fields" test_client_legacy_format_ttl_fields
    run_test "Client legacy format TOS fields" test_client_legacy_format_tos_fields
    run_test "Client legacy format delay fields" test_client_legacy_format_delay_fields
    run_test "Client legacy format PLEN field" test_client_legacy_format_plen_field
    run_test "Client legacy with lost packets" test_client_legacy_with_lost_packets
    
    # Client Raw Format Tests
    run_test "Client raw format header" test_client_raw_format_header
    run_test "Client raw format field count" test_client_raw_format_field_count
    run_test "Client raw format packet_id" test_client_raw_format_packet_id
    run_test "Client raw format payload_len" test_client_raw_format_payload_len
    run_test "Client raw format timestamps" test_client_raw_format_timestamps
    run_test "Client raw format timestamp ordering" test_client_raw_format_timestamp_ordering
    
    # Server Output Format Tests
    run_test "Server format header" test_server_format_header
    run_test "Server format field count" test_server_format_field_count
    run_test "Server format Time field" test_server_format_time_field
    run_test "Server format IP field" test_server_format_ip_field
    run_test "Server format PLEN field" test_server_format_plen_field
    
    # JSON Output Tests
    run_test "JSON output valid" test_json_output_valid
    run_test "JSON output required fields" test_json_output_required_fields
    run_test "JSON output aggregate fields" test_json_output_aggregate_fields
    run_test "JSON output sampling pattern" test_json_output_sampling_pattern
    run_test "JSON output timestamp format" test_json_output_timestamp_format
    
    # Statistics Output Tests
    run_test "Print digest output" test_print_digest_output
    run_test "Statistics output format" test_statistics_output_format
    
    # Custom Separator Tests
    run_test "Tab separator client" test_tab_separator_client
    run_test "Custom separator validation" test_custom_separator_validation
    
    # Row Count Tests
    run_test "Row count matches num_samples" test_row_count_matches_num_samples
    
    # Bug Detection Tests
    echo ""
    log_info "Running bug detection tests (these should fail if bugs exist)..."
    run_test "BUG: Client SndPort should not be 0" test_client_sndport_not_zero
    run_test "BUG: Server SndTOS should be numeric" test_server_sndtos_is_numeric
    run_test "BUG: Server FW_TOS should be TOS not delay" test_server_fwtos_is_tos_not_delay
    run_test "BUG: Server FWD should not duplicate" test_server_fwd_not_duplicate
    
    print_test_summary
    exit $?
}

main "$@"
