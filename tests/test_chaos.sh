#!/usr/bin/env bash
# test_chaos.sh (TV4) - Chaos / Resilience test: payload bien, du lieu rac, kill -9, qua tai
# Chay: make test-chaos   (hoac: bash tests/test_chaos.sh)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
require_bins server client ipc_cleanup raw_send
trap cleanup_on_exit EXIT
RAW="$BIN/raw_send"

echo "${B}CHAOS TEST - Shared Message Queues${N}"
start_server || exit 1

# raw <args...>: gui message tho, in response; khong lam script dung khi raw_send tra exit != 0
raw() { timeout 10 "$RAW" "$@" 2>&1 || true; }
alive_check() {  # sau moi don tan cong, server phai con song va tra loi PING
    run_client "PING"
    if server_alive && [[ "$OUT" == *PONG* ]]; then ok "$1 -> server van song, PING van PONG"
    else bad "$1 -> server khong con hoat dong dung" "$OUT"; fi
}

section "1. Payload o bien 256 byte"
out=$(raw --len 256 "{PID}|UPPER|x");   ok "Gui dung 256 byte: nhan '$(echo "$out" | cut -c1-40)...'"
alive_check "Message 256 byte"
out=$(raw --len 257 "{PID}|UPPER|x")
assert_contains "Message 257 byte bi kernel tu choi (EMSGSIZE), khong den server" "$out" "SEND_ERROR"
alive_check "Message 257 byte"
# Ghi chu: PROTO_MAX_PAYLOAD_LEN=224 gom ca byte '\0' nen payload hop le toi da la 223 ky tu
p223=$(printf 'a%.0s' $(seq 1 223)); out=$(raw "{PID}|UPPER|$p223")
assert_contains "Payload 223 ky tu (toi da hop le) duoc xu ly" "$out" "OK|"
p224=$(printf 'a%.0s' $(seq 1 224)); out=$(raw "{PID}|UPPER|$p224")
assert_contains "Payload 224 ky tu tra ERROR|BUFFER_OVERFLOW, khong crash" "$out" "BUFFER_OVERFLOW"
alive_check "Payload 223/224 ky tu"

section "2. Request sai dinh dang / du lieu rac"
for m in "|||" "" "abc" "{PID}" "{PID}|" "{PID}|PING" "{PID}||" "||PING|" "abc|PING|" "-5|PING|" "{PID}|PING|||||||" "{PID}|ping|" "{PID}|FIB|99999999999999999999"; do
    out=$(raw "$m"); ok "Gui '$m' -> '$(echo "$out" | head -c 50)'"
done
alive_check "Request sai dinh dang"
out=$(raw --no-wait "99999999|PING|");  ok "PID tro toi queue khong ton tai (server phai bo qua)"
sleep 0.3; alive_check "Response gui toi queue khong ton tai"

section "3. Ky tu dac biet / tan cong injection"
for s in '$(id)' '`id`' '%s%s%s%n' "'; rm -rf /" '../../etc/passwd' $'tab\there' 'xin chào' '\x41\x42'; do
    raw "{PID}|UPPER|$s" >/dev/null
done
out=$(raw '{PID}|UPPER|$(id)');         assert_contains "Khong thuc thi \$(id) - chi tra ve chu hoa" "$out" 'OK|'
if [[ "$out" != *uid=* ]]; then ok "Khong ro ri ket qua lenh 'uid='"; else bad "Khong ro ri ket qua lenh 'uid='" "$out"; fi
out=$(raw '{PID}|UPPER|%s%s%n');        assert_contains "Format string %s%n khong lam crash server" "$out" "OK|"
alive_check "Ky tu dac biet"

section "4. kill -9 client dang giu queue response"
"$RAW" --hold 30 "{PID}|PING|" >/dev/null 2>&1 &
cpid=$!; sleep 0.5
assert_true "Queue response cua client ton tai khi client dang chay" "[[ -e $MQ_DIR/shared_responses_$cpid ]]"
kill -9 "$cpid"; wait "$cpid" 2>/dev/null
assert_true "Sau kill -9 queue bi mo coi con lai trong /dev/mqueue (dung ly thuyet)" "[[ -e $MQ_DIR/shared_responses_$cpid ]]"
"$BIN/ipc_cleanup" --pid "$cpid" >/dev/null 2>&1
assert_true "ipc_cleanup --pid don sach queue mo coi" "[[ ! -e $MQ_DIR/shared_responses_$cpid ]]"
alive_check "Client bi kill -9"
"$RAW" --hold 30 "{PID}|PING|" >/dev/null 2>&1 &
cpid=$!; sleep 0.4; kill -9 "$cpid"; wait "$cpid" 2>/dev/null
"$BIN/ipc_cleanup" --all >/dev/null 2>&1
assert_true "ipc_cleanup --all don moi queue shared_* (ke ca queue request)" "[[ -z \"\$(leftover_queues)\" ]]"
server_alive && kill -INT "$SERVER_PID"; sleep 1; SERVER_PID=""
start_server && ok "Khoi dong lai server sau khi don queue"

section "5. kill -9 server"
kill -9 "$SERVER_PID"; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""
assert_true "Sau kill -9 server, /shared_requests van ton tai trong kernel" "[[ -e $MQ_DIR/shared_requests ]]"
start=$(date +%s); run_client "PING"; el=$(( $(date +%s) - start ))
assert_true "Client khi server chet: loi/timeout trong <= 10s, khong treo vo han" "[[ $CLIENT_RC -ne 0 && $el -le 10 ]]"
assert_true "Client don sach queue response cua minh sau loi" "[[ -z \"\$(ls $MQ_DIR | grep shared_responses_)\" ]]"
purge_queues
start_server && ok "Server khoi dong lai binh thuong sau khi bi kill -9" && alive_check "Server sau khoi phuc"

section "6. Qua tai: 60 client dong thoi (FIB 35)"
tmpd=$(mktemp -d); cpids=()
for i in $(seq 1 60); do
    ( timeout 30 "$BIN/client" --cmd "FIB 35" --prio NORMAL >"$tmpd/$i.out" 2>&1 ) & cpids+=($!)
done
for p in "${cpids[@]}"; do wait "$p"; done
okc=$(grep -l "|9227465" "$tmpd"/*.out 2>/dev/null | wc -l)
echo "   (${okc}/60 client nhan dung ket qua; phan con lai co the timeout 3s hop le do hang doi day)"
wrong=$(grep -L -e "|9227465" -e "imeout" -e "rror" -e "ailed" "$tmpd"/*.out 2>/dev/null | wc -l)
assert_true "It nhat 1 client thanh cong khi qua tai" "[[ $okc -ge 1 ]]"
assert_eq "Khong client nao nhan ket qua sai (khong lan response)" "$wrong" "0"
rm -rf "$tmpd"
sleep 1; alive_check "Qua tai 60 client"
assert_true "Khong con queue response mo coi sau qua tai" "[[ -z \"\$(ls $MQ_DIR | grep shared_responses_)\" ]]"

section "7. Tat server"
stop_server; assert_eq "Server thoat sach sau SIGINT" "$?" "0"
assert_eq "Khong con queue shared_* nao" "$(leftover_queues)" ""

check_sanitizer_logs
summary "CHAOS"
