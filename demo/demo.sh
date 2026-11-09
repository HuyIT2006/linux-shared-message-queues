
#!/usr/bin/env bash

# POSIX Message Queue - Demonstration Script
# Team Member 3: Client, Benchmark and Demo

set -u

CLIENT="./bin/client"
BENCHMARK="./bin/benchmark"

echo "========================================"
echo " POSIX MESSAGE QUEUE - PROJECT DEMO"
echo "========================================"

# Check project directory and executables
if [[ ! -x "$CLIENT" || ! -x "$BENCHMARK" ]]; then
    echo "[ERROR] Client or Benchmark executable not found."
    echo "Please run this script from the project root."
    exit 1
fi

# Check whether the server is running
if ! pgrep -f '^\./bin/server([[:space:]]|$)' >/dev/null; then
    echo "[ERROR] Server is not running."
    echo "Start the server in another terminal:"
    echo "  ./bin/server"
    exit 1
fi

run_demo() {
    local title="$1"
    shift

    echo
    echo "----------------------------------------"
    echo "$title"
    echo "----------------------------------------"

    if ! "$@"; then
        echo "[ERROR] Demo step failed: $title"
        exit 1
    fi
}

run_demo "DEMO 1: PING / PONG" \
    "$CLIENT" --cmd "PING" --prio NORMAL

run_demo "DEMO 2: FIBONACCI" \
    "$CLIENT" --cmd "FIB 10" --prio HIGH

run_demo "DEMO 3: UPPERCASE" \
    "$CLIENT" --cmd "UPPER hello linux" --prio LOW

run_demo "DEMO 4: URGENT PRIORITY" \
    "$CLIENT" --cmd "PING" --prio URGENT

run_demo "DEMO 5: BENCHMARK - 1 CLIENT" \
    "$BENCHMARK" --clients 1 --requests 10 \
    --cmd PING --prio NORMAL

run_demo "DEMO 6: BENCHMARK - 5 CLIENTS" \
    "$BENCHMARK" --clients 5 --requests 10 \
    --cmd PING --prio NORMAL

run_demo "DEMO 7: BENCHMARK - 10 CLIENTS" \
    "$BENCHMARK" --clients 10 --requests 10 \
    --cmd PING --prio NORMAL

run_demo "DEMO 8: BENCHMARK - 20 CLIENTS" \
    "$BENCHMARK" --clients 20 --requests 10 \
    --cmd PING --prio NORMAL

echo
echo "========================================"
echo " AUTOMATED DEMO COMPLETED SUCCESSFULLY"
echo "========================================"
echo
echo "Interactive REPL can be demonstrated with:"
echo "  ./bin/client"
echo
