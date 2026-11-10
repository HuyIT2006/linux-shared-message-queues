#!/usr/bin/env bash
# test_integration.sh (TV4) - Test tu dong toan he thong: server + client + benchmark
# Chay: make test-integration   (hoac: bash tests/test_integration.sh)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_bins server client benchmark ipc_cleanup
trap cleanup_on_exit EXIT

echo "${B}INTEGRATION TEST - Shared Message Queues${N}"
start_server || exit 1
ok "Server khoi dong, queue /shared_requests ton tai"

section "1. Cac command co ban"
run_client "PING"; out=$OUT;        assert_contains "PING -> PONG" "$out" "PONG"
assert_eq "PING exit code = 0" "$CLIENT_RC" "0"
run_client "TIME"; out=$OUT
if [[ "$out" =~ [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2} ]]; then ok "TIME tra ve dinh dang ISO 8601"; else bad "TIME tra ve dinh dang ISO 8601" "$out"; fi
for pair in "0:0" "1:1" "10:55" "20:6765" "45:1134903170"; do
    n=${pair%%:*}; exp=${pair##*:}
    run_client "FIB $n"; out=$OUT; assert_contains "FIB $n = $exp" "$out" "|$exp"
done
run_client "UPPER xin-chao"; out=$OUT; assert_contains "UPPER xin-chao -> XIN-CHAO" "$out" "XIN-CHAO"
run_client "UPPER hello linux"; out=$OUT; assert_contains "UPPER co khoang trang" "$out" "HELLO LINUX"
run_client "SYSINFO"; out=$OUT;     assert_contains "SYSINFO co UPTIME" "$out" "UPTIME="
assert_contains "SYSINFO co LOAD" "$out" "LOAD="
run_client "STATS"; out=$OUT;       assert_contains "STATS co TOTAL" "$out" "TOTAL="

section "2. Muc do uu tien (priority)"
for p in LOW NORMAL HIGH URGENT; do
    run_client "PING" "$p"; out=$OUT; assert_contains "PING voi priority $p" "$out" "PONG"
done

section "3. Xu ly loi (server khong duoc crash)"
run_client "FOO"; out=$OUT;         assert_contains "Lenh khong hop le -> UNKNOWN_COMMAND" "$out" "UNKNOWN_COMMAND"
assert_true "Lenh sai -> client exit code != 0" "[[ $CLIENT_RC -ne 0 ]]"
for bad_arg in "FIB 46" "FIB -1" "FIB abc" "FIB"; do
    run_client "$bad_arg"; out=$OUT; assert_contains "'$bad_arg' -> INVALID_ARGUMENT" "$out" "INVALID_ARGUMENT"
done
run_client "UPPER $(printf 'a%.0s' $(seq 1 300))"; out=$OUT
assert_true "Payload > gioi han bi tu choi (client exit != 0)" "[[ $CLIENT_RC -ne 0 ]]"
assert_true "Server van song sau cac loi tren" "server_alive"
run_client "PING"; out=$OUT;        assert_contains "Server van tra loi PING sau loi" "$out" "PONG"

section "4. Nhieu client dong thoi - response khong bi lan"
tmpd=$(mktemp -d)
cpids=()
for i in $(seq 1 20); do
    ( timeout 15 "$BIN/client" --cmd "UPPER user$i" --prio NORMAL >"$tmpd/$i.out" 2>&1 ) &
    cpids+=($!)
done
for p in "${cpids[@]}"; do wait "$p"; done
mixed=0; missing=0
for i in $(seq 1 20); do
    if grep -q "USER$i\$" "$tmpd/$i.out"; then :; else
        grep -q "USER" "$tmpd/$i.out" && mixed=$((mixed+1)) || missing=$((missing+1))
    fi
done
assert_eq "20 client dong thoi: khong client nao nhan nham response" "$mixed" "0"
assert_eq "20 client dong thoi: khong client nao mat response" "$missing" "0"
rm -rf "$tmpd"

section "5. Benchmark"
out=$(timeout 60 "$BIN/benchmark" --clients 5 --requests 20 --cmd PING --prio NORMAL 2>&1)
assert_contains "Benchmark 5x20: Successful requests 100" "$out" "Successful requests: 100"
assert_contains "Benchmark 5x20: Failed requests 0" "$out" "Failed requests: 0"
assert_contains "Benchmark in Throughput" "$out" "Throughput"

section "6. Cong cu ipc_cleanup"
out=$("$BIN/ipc_cleanup" --list 2>&1); assert_contains "ipc_cleanup --list thay /shared_requests" "$out" "shared_requests"

section "7. Graceful shutdown & ro ri queue"
assert_true "Khong co queue response mo coi khi server dang chay" "[[ -z \"\$(ls $MQ_DIR | grep shared_responses_)\" ]]"
stop_server; rc=$?
assert_eq "Server thoat sach sau SIGINT (khong phai kill -9)" "$rc" "0"
assert_eq "Khong con queue shared_* trong /dev/mqueue sau khi tat server" "$(leftover_queues)" ""
run_client "PING"; out=$OUT
assert_true "Khi server tat, client bao loi/timeout thay vi treo" "[[ $CLIENT_RC -ne 0 ]]"

check_sanitizer_logs
summary "INTEGRATION"
