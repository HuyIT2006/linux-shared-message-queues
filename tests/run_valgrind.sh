#!/usr/bin/env bash
# run_valgrind.sh (TV4) - Chay Valgrind Memcheck cho unit test + server/client.
# Yeu cau: sudo apt install valgrind. Muc tieu: "0 errors from 0 contexts", "no leaks are possible".
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
command -v valgrind >/dev/null || { echo "${R}Chua cai valgrind: sudo apt install valgrind${N}"; exit 2; }
require_bins server client test_ipc_queue test_protocol
trap cleanup_on_exit EXIT
VG=(valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=99 -q)

section "1. Unit test duoi Valgrind"
for t in test_ipc_queue test_protocol; do
    "${VG[@]}" "$BIN/$t" >"$LOG_DIR/vg_$t.log" 2>&1 && ok "$t: khong loi bo nho" || bad "$t: Valgrind bao loi" "xem $LOG_DIR/vg_$t.log"
done

section "2. Server duoi Valgrind + tai tu client"
purge_queues
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=99 \
    --log-file="$LOG_DIR/vg_server.log" "$BIN/server" >"$LOG_DIR/vg_server.out" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do [[ -e "$MQ_DIR/shared_requests" ]] && break; sleep 0.1; done
for c in "PING" "TIME" "FIB 30" "UPPER hello" "SYSINFO" "STATS" "FOO" "FIB 99"; do run_client "$c"; done
timeout 60 "$BIN/benchmark" --clients 10 --requests 20 --cmd PING >/dev/null 2>&1
stop_server; rc=$?
assert_eq "Server tat sach bang SIGINT duoi Valgrind" "$rc" "0"
assert_contains "Server: khong co loi bo nho" "$(cat "$LOG_DIR/vg_server.log")" "ERROR SUMMARY: 0 errors from 0 contexts"
assert_contains "Server: khong ro ri bo nho" "$(cat "$LOG_DIR/vg_server.log")" "All heap blocks were freed"

section "3. Client duoi Valgrind (can server chay)"
start_server
for c in "PING" "FIB 20" "FOO"; do
    valgrind --leak-check=full --error-exitcode=99 -q --log-file="$LOG_DIR/vg_client.log" "$BIN/client" --cmd "$c" >/dev/null 2>&1
    [[ -s "$LOG_DIR/vg_client.log" ]] && bad "client '$c': Valgrind co canh bao" "xem $LOG_DIR/vg_client.log" || ok "client '$c': sach"
done
stop_server
summary "VALGRIND"
