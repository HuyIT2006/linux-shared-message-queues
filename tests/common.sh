#!/usr/bin/env bash
# common.sh - ham dung chung cho test_integration.sh / test_chaos.sh (TV4)
# Duoc `source` boi cac script test, khong chay truc tiep.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin"
MQ_DIR="/dev/mqueue"
LOG_DIR="$ROOT/logs"
SERVER_LOG="$LOG_DIR/server_$$.log"; rm -f "$LOG_DIR"/server_*.log
SERVER_PID=""

PASS=0; FAIL=0; FAILED_NAMES=()

if [[ -t 1 ]]; then G=$'\e[32m'; R=$'\e[31m'; Y=$'\e[33m'; B=$'\e[1m'; N=$'\e[0m'
else G=""; R=""; Y=""; B=""; N=""; fi

mkdir -p "$LOG_DIR"

# Neu co tien trinh server cu chua tat -> bao loi som, tranh test sai
require_bins() {
    for b in "$@"; do
        [[ -x "$BIN/$b" ]] || { echo "${R}Thieu $BIN/$b. Chay 'make' truoc.${N}"; exit 2; }
    done
}

# Xoa moi queue cua du an (dung ipc_cleanup cua TV1)
purge_queues() { "$BIN/ipc_cleanup" --all >/dev/null 2>&1 || true; }

leftover_queues() { ls "$MQ_DIR" 2>/dev/null | grep -E '^shared_' || true; }

start_server() {
    purge_queues
    "$BIN/server" >"$SERVER_LOG" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do               # doi toi da 5s
        [[ -e "$MQ_DIR/shared_requests" ]] && return 0
        kill -0 "$SERVER_PID" 2>/dev/null || break
        sleep 0.1
    done
    echo "${R}Server khong khoi dong duoc. Xem $SERVER_LOG${N}"; return 1
}

server_alive() { [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; }

# Tat server bang SIGINT (graceful), cho toi da 10s
stop_server() {
    server_alive || { SERVER_PID=""; return 0; }
    kill -INT "$SERVER_PID" 2>/dev/null
    for _ in $(seq 1 100); do
        kill -0 "$SERVER_PID" 2>/dev/null || { wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""; return 0; }
        sleep 0.1
    done
    kill -9 "$SERVER_PID" 2>/dev/null; SERVER_PID=""; return 1
}

cleanup_on_exit() {
    [[ -n "$SERVER_PID" ]] && kill -9 "$SERVER_PID" 2>/dev/null
    jobs -p | xargs -r kill -9 2>/dev/null
    purge_queues
}

# ---- Assertion --------------------------------------------------------------
ok()   { PASS=$((PASS+1)); echo "  [${G}PASS${N}] $1"; }
bad()  { FAIL=$((FAIL+1)); FAILED_NAMES+=("$1"); echo "  [${R}FAIL${N}] $1"; [[ -n "${2:-}" ]] && echo "         -> $2"; }

# assert_contains <ten test> <chuoi thuc te> <chuoi mong doi>
assert_contains() {
    if [[ "$2" == *"$3"* ]]; then ok "$1"; else bad "$1" "mong doi chua '$3', thuc te: '$(echo "$2" | tr '\n' ' ' | cut -c1-160)'"; fi
}
assert_eq() { if [[ "$2" == "$3" ]]; then ok "$1"; else bad "$1" "mong doi '$3', thuc te '$2'"; fi; }
assert_true() { if eval "$2"; then ok "$1"; else bad "$1" "dieu kien sai: $2"; fi; }

# run_client <lenh> [prio] -> dat bien toan cuc OUT (output) va CLIENT_RC (exit code)
# Khong goi trong $(...) vi subshell se lam mat CLIENT_RC.
run_client() {
    OUT=$(timeout 15 "$BIN/client" --cmd "$1" --prio "${2:-NORMAL}" 2>&1); CLIENT_RC=$?
}

section() { echo; echo "${B}== $1 ==${N}"; }

summary() {
    echo; echo "${B}================ KET QUA: $1 ================${N}"
    echo "  PASSED: ${G}$PASS${N}   FAILED: ${R}$FAIL${N}"
    for n in "${FAILED_NAMES[@]}"; do echo "   - $n"; done
    [[ $FAIL -eq 0 ]]
}

# Neu build bang `make debug`, tim loi ASan/UBSan trong log cua server
check_sanitizer_logs() {
    if grep -qE "AddressSanitizer|runtime error|LeakSanitizer" "$LOG_DIR"/server_*.log 2>/dev/null; then
        bad "ASan/UBSan phat hien loi trong log server" "xem $LOG_DIR/server_*.log"
    else ok "Log server khong co loi ASan/UBSan"; fi
}
