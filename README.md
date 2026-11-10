# Shared Message Queues trong Linux IPC (C, POSIX Message Queue)

Đồ án mô phỏng giao tiếp liên tiến trình (IPC) trên Linux bằng **POSIX Message Queue**.
Một **server đa luồng (worker thread pool)** nhận request từ **nhiều client đồng thời** qua
queue dùng chung `/shared_requests`; mỗi client nhận response qua queue riêng
`/shared_responses_<PID>`.

## 1. Kiến trúc tóm tắt

```text
Client A ─┐
          ├──> /shared_requests ──> Server (listener + worker pool)
Client B ─┤                            │
          │                            ├──> /shared_responses_<PID_A>
Client C ─┘                            └──> /shared_responses_<PID_B>
```

Luồng hoạt động:

1. Client tạo queue response riêng `/shared_responses_<PID>`.
2. Client gửi request vào `/shared_requests` (kèm mức ưu tiên).
3. Server nhận, phân tích và đưa vào hàng đợi công việc; một worker thực thi.
4. Worker gửi response vào đúng queue của client có PID tương ứng.
5. Client nhận response (có timeout), in kết quả, đóng và xóa queue của mình.
6. Khi server nhận `SIGINT`/`SIGTERM`: dừng nhận request mới, chờ worker xong việc, xóa `/shared_requests`, thoát sạch.

Chi tiết xem `docs/ARCHITECTURE.md` và so sánh IPC trong `docs/IPC_COMPARISON.md`.

## 2. Yêu cầu môi trường

- Linux (Ubuntu) hoặc WSL2. Cần hỗ trợ POSIX message queue (`/dev/mqueue`).
- Công cụ: `gcc`, `make`, `git`, `valgrind` (tùy chọn, để kiểm tra rò rỉ bộ nhớ).

```bash
sudo apt update
sudo apt install -y build-essential git valgrind
```

## 3. Lấy mã nguồn và biên dịch

```bash
git clone https://github.com/HuyIT2006/linux-shared-message-queues.git
cd linux-shared-message-queues
make clean && make
```

Biên dịch với `-std=gnu11 -Wall -Wextra -Werror -pedantic`, sinh ra trong `bin/`:

| Binary | Vai trò |
|---|---|
| `bin/server` | Server đa luồng |
| `bin/client` | Client (one-shot hoặc REPL) |
| `bin/benchmark` | Công cụ đo tải |
| `bin/ipc_cleanup` | Liệt kê và dọn queue mồ côi |
| `bin/test_ipc_queue`, `bin/test_protocol` | Unit test |
| `bin/raw_send` | Công cụ gửi message thô cho chaos test |

## 4. Chạy chương trình

**Terminal 1 – Server:**

```bash
./bin/server
```

Dừng server bằng `Ctrl+C` (graceful shutdown, tự xóa queue request).

**Terminal 2 – Client:**

```bash
./bin/client --cmd "PING"
./bin/client --cmd "FIB 20" --prio HIGH
./bin/client --cmd "UPPER xin chao" --prio LOW
./bin/client --cmd "SYSINFO"
./bin/client            # không tham số: chế độ REPL tương tác (gõ exit hoặc quit để thoát)
```

Mức ưu tiên (`--prio`): `LOW (0)`, `NORMAL (10)`, `HIGH (20)`, `URGENT (30)`.

### Các lệnh server hỗ trợ

| Lệnh | Mô tả | Ví dụ response |
|---|---|---|
| `PING` | Kiểm tra server còn sống | `OK\|0.001\|PONG` |
| `TIME` | Thời gian hệ thống (ISO 8601) | `OK\|0.035\|2026-11-10T11:34:40+0000` |
| `FIB <n>` | Fibonacci, `0 <= n <= 45` | `OK\|0.001\|6765` |
| `UPPER <chuỗi>` | Chuyển chữ hoa | `OK\|0.001\|ABC` |
| `SYSINFO` | Đọc `/proc/uptime`, `/proc/loadavg`, `/proc/meminfo` | `OK\|...\|UPTIME=..,LOAD=..,MEM_AVAIL=..` |
| `STATS` | Số liệu vận hành server | `OK\|...\|TOTAL=..,ERRORS=..,WORKERS=..` |
| `CALC <biểu thức>` | Tính toán đơn giản | `OK\|...\|500` |

Lỗi trả về dạng `ERROR|<thời gian>|<mã lỗi>`, các mã: `UNKNOWN_COMMAND`, `INVALID_ARGUMENT`,
`INVALID_REQUEST`, `BUFFER_OVERFLOW`.

### Protocol

```text
Request : <PID>|<COMMAND>|<ARGUMENT>              (3 trường)
          <PID>|<COMMAND>|<PRIORITY>|<PAYLOAD>    (4 trường, mở rộng)
Response: <STATUS>|<EXEC_TIME_MS>|<PAYLOAD>
```

Giới hạn: một message tối đa **256 byte**; payload hợp lệ tối đa **223 ký tự** (giới hạn 224 đã gồm byte kết thúc `\0`).

### Benchmark

```bash
./bin/benchmark --clients 10 --requests 50 --cmd PING --prio NORMAL
```

Tham số: `--clients N` (số tiến trình client), `--requests N` (số request mỗi client),
`--cmd`, `--prio`. Cần server đang chạy.

### Dọn queue mồ côi (sau khi tiến trình bị `kill -9`)

```bash
./bin/ipc_cleanup --list          # liệt kê queue trong /dev/mqueue
./bin/ipc_cleanup --pid <PID>     # xóa /shared_responses_<PID>
./bin/ipc_cleanup --all           # xóa mọi queue của dự án (/shared_*)
```

### Demo tự động

Chạy server ở terminal khác rồi:

```bash
./demo/demo.sh
```

## 5. Kiểm thử

| Lệnh | Nội dung |
|---|---|
| `make test-unit` | Unit test IPC core và Protocol/Worker pool |
| `make test-integration` | Test tích hợp: các lệnh, priority, lỗi, 20 client đồng thời, benchmark, graceful shutdown, không rò queue |
| `make test-chaos` | Chaos test: payload ở biên, request rác, thử injection, `kill -9` client/server, quá tải 60 client |
| `make test` | Chạy cả ba nhóm trên |
| `make sanitize` | Build bản debug (AddressSanitizer + UBSan) rồi chạy toàn bộ test |
| `make valgrind` | Valgrind Memcheck cho unit test, server và client |
| `make debug` | Chỉ build bản debug có sanitizer |
| `make clean` | Xóa file build |

Kết quả kiểm thử trên Ubuntu (VirtualBox):

| Bộ test | Kết quả |
|---|---|
| Unit test IPC core | 44/44 |
| Unit test Protocol và Worker pool | 52/52 |
| Integration | 38/38 |
| Chaos | 45/45 |
| Valgrind (unit, server, client) | 8/8, không lỗi bộ nhớ |

Log do các script sinh ra nằm trong thư mục `logs/` (không commit lên git).

> Lưu ý: nếu `/dev/mqueue` chưa được mount, chạy
> `sudo mkdir -p /dev/mqueue && sudo mount -t mqueue none /dev/mqueue`.

## 6. Cấu trúc thư mục

```text
linux-shared-message-queues/
├── Makefile
├── README.md
├── PHAN_CONG_NHIEM_VU.md
├── CAU_TRUC_YEU_CAU.md
├── include/        ipc_queue.h, protocol.h, worker_pool.h
├── src/            ipc_queue.c, ipc_cleanup.c, protocol.c, worker_pool.c,
│                   server.c, client.c, benchmark.c
├── demo/           demo.sh
├── tests/          test_ipc_queue.c, test_protocol.c, test_integration.sh,
│                   test_chaos.sh, run_valgrind.sh, common.sh, raw_send.c
└── docs/           ARCHITECTURE.md, IPC_COMPARISON.md
```

## 7. Thành viên và phân công

| Thành viên | Phụ trách |
|---|---|
| TV1 | IPC core: `ipc_queue`, `mq_notify`, `ipc_cleanup` |
| TV2 | Server, worker thread pool, protocol, system services |
| TV3 | Client (one-shot và REPL), benchmark, demo |
| TV4 | Build system, test tích hợp và chaos, Valgrind/ASan, tài liệu, slide |

Chi tiết xem `PHAN_CONG_NHIEM_VU.md`.
