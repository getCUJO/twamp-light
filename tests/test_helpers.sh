#!/bin/bash
# Test helper functions for twamp-light integration tests

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"

# Paths to executables
CLIENT="${BUILD_DIR}/twamp-light-client"
SERVER="${BUILD_DIR}/twamp-light-server"

# Test output files
TEST_OUTPUT_DIR="${BUILD_DIR}/test_output"
SERVER_OUTPUT="${TEST_OUTPUT_DIR}/server_output.txt"
CLIENT_OUTPUT="${TEST_OUTPUT_DIR}/client_output.txt"
SERVER_PID_FILE="${TEST_OUTPUT_DIR}/server.pid"

# Default test parameters
DEFAULT_PORT=14200
DEFAULT_NUM_SAMPLES=5
DEFAULT_TIMEOUT=5

# Color output (if terminal supports it)
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    NC='\033[0m' # No Color
else
    RED=''
    GREEN=''
    YELLOW=''
    NC=''
fi

# ============================================================================
# Logging functions
# ============================================================================

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_test() {
    echo -e "${GREEN}[TEST]${NC} $1"
}

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
}

# ============================================================================
# Setup and cleanup functions
# ============================================================================

setup_test_dir() {
    mkdir -p "${TEST_OUTPUT_DIR}"
    rm -f "${SERVER_OUTPUT}" "${CLIENT_OUTPUT}" "${SERVER_PID_FILE}"
}

cleanup() {
    # Kill any running server
    if [ -f "${SERVER_PID_FILE}" ]; then
        local pid
        pid=$(cat "${SERVER_PID_FILE}")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
        fi
        rm -f "${SERVER_PID_FILE}"
    fi
    
    # Kill any stray twamp processes started by this test
    pkill -f "twamp-light-server.*${DEFAULT_PORT}" 2>/dev/null || true
    pkill -f "twamp-light-client.*${DEFAULT_PORT}" 2>/dev/null || true
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

# ============================================================================
# Server management functions
# ============================================================================

start_server() {
    local port="${1:-$DEFAULT_PORT}"
    local num_samples="${2:-$DEFAULT_NUM_SAMPLES}"
    local extra_args="${3:-}"
    
    setup_test_dir
    
    # shellcheck disable=SC2086
    stdbuf -o0 "${SERVER}" -n "${num_samples}" -P "${port}" ${extra_args} &>"${SERVER_OUTPUT}" &
    local server_pid=$!
    echo "${server_pid}" > "${SERVER_PID_FILE}"
    
    # Wait for server to start
    sleep 0.5
    
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        log_error "Server failed to start"
        cat "${SERVER_OUTPUT}"
        return 1
    fi
    
    log_info "Server started on port ${port} (PID: ${server_pid})"
    return 0
}

stop_server() {
    if [ -f "${SERVER_PID_FILE}" ]; then
        local pid
        pid=$(cat "${SERVER_PID_FILE}")
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            wait "$pid" 2>/dev/null
            log_info "Server stopped (PID: ${pid})"
        fi
        rm -f "${SERVER_PID_FILE}"
    fi
}

wait_for_server() {
    local port="${1:-$DEFAULT_PORT}"
    local timeout="${2:-5}"
    local count=0
    
    while [ $count -lt $timeout ]; do
        if nc -z localhost "$port" 2>/dev/null; then
            return 0
        fi
        sleep 1
        count=$((count + 1))
    done
    
    log_error "Timeout waiting for server on port ${port}"
    return 1
}

# ============================================================================
# Client execution functions
# ============================================================================

run_client() {
    local port="${1:-$DEFAULT_PORT}"
    local num_samples="${2:-$DEFAULT_NUM_SAMPLES}"
    local extra_args="${3:-}"
    
    # Put address before extra_args to avoid -l option consuming it
    # shellcheck disable=SC2086
    stdbuf -o0 "${CLIENT}" -n "${num_samples}" -i 10 "localhost:${port}" ${extra_args} &>"${CLIENT_OUTPUT}"
    local exit_code=$?
    
    return ${exit_code}
}

run_client_background() {
    local port="${1:-$DEFAULT_PORT}"
    local num_samples="${2:-$DEFAULT_NUM_SAMPLES}"
    local extra_args="${3:-}"
    
    # Put address before extra_args to avoid -l option consuming it
    # shellcheck disable=SC2086
    stdbuf -o0 "${CLIENT}" -n "${num_samples}" -i 10 "localhost:${port}" ${extra_args} &>"${CLIENT_OUTPUT}" &
    echo $!
}

# ============================================================================
# Output validation functions
# ============================================================================

validate_csv_header() {
    local file="$1"
    local expected_header="$2"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    local actual_header
    actual_header=$(head -n 1 "$file")
    
    if [ "$actual_header" != "$expected_header" ]; then
        log_error "Header mismatch"
        log_error "Expected: $expected_header"
        log_error "Actual:   $actual_header"
        return 1
    fi
    
    return 0
}

validate_field_count() {
    local file="$1"
    local expected_count="$2"
    local separator="${3:-,}"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    # Check field count for each non-header line
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local field_count
        field_count=$(echo "$line" | awk -F"$separator" '{print NF}')
        
        if [ "$field_count" -ne "$expected_count" ]; then
            log_error "Field count mismatch on line $line_num: expected $expected_count, got $field_count"
            return 1
        fi
    done < "$file"
    
    return 0
}

validate_row_count() {
    local file="$1"
    local expected_count="$2"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    # Count non-header lines
    local actual_count
    actual_count=$(tail -n +2 "$file" | wc -l | tr -d ' ')
    
    if [ "$actual_count" -ne "$expected_count" ]; then
        log_error "Row count mismatch: expected $expected_count, got $actual_count"
        return 1
    fi
    
    return 0
}

validate_numeric_field() {
    local file="$1"
    local field_num="$2"
    local separator="${3:-,}"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local value
        value=$(echo "$line" | cut -d"$separator" -f"$field_num")
        
        # Check if value is numeric (integer or floating point)
        if ! [[ "$value" =~ ^-?[0-9]+\.?[0-9]*$ ]]; then
            log_error "Non-numeric value '$value' in field $field_num on line $line_num"
            return 1
        fi
    done < "$file"
    
    return 0
}

validate_ip_field() {
    local file="$1"
    local field_num="$2"
    local separator="${3:-,}"
    local expected_ip="${4:-127.0.0.1}"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local value
        value=$(echo "$line" | cut -d"$separator" -f"$field_num")
        
        if [ "$value" != "$expected_ip" ]; then
            log_error "IP mismatch on line $line_num: expected '$expected_ip', got '$value'"
            return 1
        fi
    done < "$file"
    
    return 0
}

validate_range() {
    local file="$1"
    local field_num="$2"
    local min_val="$3"
    local max_val="$4"
    local separator="${5:-,}"
    
    if [ ! -f "$file" ]; then
        log_error "File not found: $file"
        return 1
    fi
    
    local line_num=0
    while IFS= read -r line; do
        line_num=$((line_num + 1))
        if [ $line_num -eq 1 ]; then
            continue  # Skip header
        fi
        
        local value
        value=$(echo "$line" | cut -d"$separator" -f"$field_num")
        
        if [ "$(echo "$value < $min_val" | bc -l)" -eq 1 ] || \
           [ "$(echo "$value > $max_val" | bc -l)" -eq 1 ]; then
            log_error "Value $value out of range [$min_val, $max_val] in field $field_num on line $line_num"
            return 1
        fi
    done < "$file"
    
    return 0
}

# ============================================================================
# JSON validation functions
# ============================================================================

validate_json_file() {
    local file="$1"
    
    if [ ! -f "$file" ]; then
        log_error "JSON file not found: $file"
        return 1
    fi
    
    if ! python3 -c "import json; json.load(open('$file'))" 2>/dev/null; then
        log_error "Invalid JSON in file: $file"
        return 1
    fi
    
    return 0
}

validate_json_field() {
    local file="$1"
    local field="$2"
    
    if ! python3 -c "
import json
import sys
data = json.load(open('$file'))
fields = '$field'.split('.')
for f in fields:
    data = data[f]
" 2>/dev/null; then
        log_error "JSON field '$field' not found in $file"
        return 1
    fi
    
    return 0
}

# ============================================================================
# Test assertion helpers
# ============================================================================

assert_exit_code() {
    local expected="$1"
    local actual="$2"
    local message="${3:-Exit code mismatch}"
    
    if [ "$actual" -ne "$expected" ]; then
        log_fail "$message: expected $expected, got $actual"
        return 1
    fi
    return 0
}

assert_file_exists() {
    local file="$1"
    local message="${2:-File should exist}"
    
    if [ ! -f "$file" ]; then
        log_fail "$message: $file"
        return 1
    fi
    return 0
}

assert_file_not_empty() {
    local file="$1"
    local message="${2:-File should not be empty}"
    
    if [ ! -s "$file" ]; then
        log_fail "$message: $file"
        return 1
    fi
    return 0
}

# ============================================================================
# Test result tracking
# ============================================================================

TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

run_test() {
    local test_name="$1"
    local test_func="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    log_test "Running: $test_name"
    
    if $test_func; then
        TESTS_PASSED=$((TESTS_PASSED + 1))
        log_pass "$test_name"
        return 0
    else
        TESTS_FAILED=$((TESTS_FAILED + 1))
        log_fail "$test_name"
        return 1
    fi
}

print_test_summary() {
    echo ""
    echo "========================================"
    echo "Test Summary"
    echo "========================================"
    echo "Tests run:    $TESTS_RUN"
    echo "Tests passed: $TESTS_PASSED"
    echo "Tests failed: $TESTS_FAILED"
    echo "========================================"
    
    if [ "$TESTS_FAILED" -gt 0 ]; then
        return 1
    fi
    return 0
}
