# HƯỚNG DẪN DEMO VÀ BÁO CÁO BẢO VỆ ĐỒ ÁN
# Shared Message Queues trong Linux IPC (C, POSIX Message Queue)

---

> **Mục đích tài liệu:**  
> Tài liệu này được thiết kế để phục vụ trực tiếp cho buổi báo cáo và bảo vệ đồ án trước giảng viên. Nội dung bao gồm:
> 1. Kịch bản demo trực quan từng bước (thao tác lệnh, kết quả quan sát, lời giải thích kỹ thuật).
> 2. Kịch bản thuyết trình bảo vệ (theo nhóm 4 thành viên hoặc cá nhân).
> 3. Bộ câu hỏi vấn đáp (Q&A Cheat Sheet) dự đoán từ giảng viên kèm câu trả lời chuẩn xác.
> 4. Checklist kiểm tra kỹ thuật trước khi bước vào phòng thi.

---

## MỤC LỤC
- [1. TỔNG QUAN DỰ ÁN & ĐIỂM SÁNG KỸ THUẬT](#1-tổng-quan-dự-án--điểm-sáng-kỹ-thuật)
- [2. CHUẨN BỊ MÔI TRƯỜNG & BIÊN DỊCH TRƯỚC DEMO](#2-chuẩn-bị-môi-trường--biên-dịch-trước-demo)
- [3. KỊCH BẢN DEMO TRỰC QUAN TỪNG BƯỚC (LIVE DEMO GUIDE)](#3-kịch-bản-demo-trực-quan-từng-bước-live-demo-guide)
  - [Thiết lập không gian làm việc (Khuyên dùng 3 Terminal)](#thiết-lập-không-gian-làm-việc-khuyên-dùng-3-terminal)
  - [Bước 1: Khởi động Server & Kiến trúc Đa luồng](#bước-1-khởi-động-server--kiến-trúc-đa-luồng)
  - [Bước 2: Client One-shot & Các Command Cốt lõi](#bước-2-client-one-shot--các-command-cốt-lõi)
  - [Bước 3: Các Dịch vụ Hệ thống Chuyên sâu (SYSINFO, STATS, CALC)](#bước-3-các-dịch-vụ-hệ-thống-chuyên-sâu-sysinfo-stats-calc)
  - [Bước 4: Trực quan hóa Cơ chế Độ ưu tiên (Message Priority)](#bước-4-trực-quan-hóa-cơ-chế-độ-ưu-tiên-message-priority)
  - [Bước 5: Giao diện tương tác dòng lệnh trực tiếp (Interactive REPL Shell)](#bước-5-giao-diện-tương-tác-dòng-lệnh-trực-tiếp-interactive-repl-shell)
  - [Bước 6: Đo tải & Đánh giá Hiệu năng (Benchmark High Concurrency)](#bước-6-đo-tải--đánh-giá-hiệu-năng-benchmark-high-concurrency)
  - [Bước 7: Cơ chế Dọn dẹp Hàng đợi Mồ côi (Orphaned Queue Cleanup)](#bước-7-cơ-chế-dọn-dẹp-hàng-đợi-mồ-côi-orphaned-queue-cleanup)
  - [Bước 8: Kiểm định Chất lượng Phần mềm (Unit Test, Integration, Sanitizers)](#bước-8-kiểm-định-chất-lượng-phần-mềm-unit-test-integration-sanitizers)
  - [Bước 9: Script Demo Tự động Toàn diện](#bước-9-script-demo-tự-động-toàn-diện)
- [4. KỊCH BẢN THUYẾT TRÌNH BÁO CÁO (ORAL PRESENTATION)](#4-kịch-bản-thuyết-trình-báo-cáo-oral-presentation)
  - [Phân chia phần thuyết trình cho 4 thành viên](#phân-chia-phần-thuyết-trình-cho-4-thành-viên)
  - [Kịch bản báo cáo ngắn gọn (Dành cho 1 người đại diện)](#kịch-bản-báo-cáo-ngắn-gọn-dành-cho-1-người-đại-diện)
- [5. BỘ CÂU HỎI VẤN ĐÁP THƯỜNG GẶP (Q&A CHEAT SHEET)](#5-bộ-câu-hỏi-vấn-đáp-thường-gặp-qa-cheat-sheet)
- [6. BẢNG TỔNG HỢP CÁC LỆNH NHANH CHO BUỔI BẢO VỆ](#6-bảng-tổng-hợp-các-lệnh-nhanh-cho-buổi-bảo-vệ)

---

## 1. TỔNG QUAN DỰ ÁN & ĐIỂM SÁNG KỸ THUẬT

### 1.1. Bài toán và Kiến trúc Giải pháp
Dự án giải quyết bài toán giao tiếp giữa nhiều tiến trình (IPC - Inter-Process Communication) độc lập trên Linux bằng **POSIX Message Queue (`mq_*`)**:
- **Hàng đợi Request dùng chung (`/shared_requests`):** Đóng vai trò là cổng tiếp nhận tập trung (MPSC: Multiple Producers - Single Consumer). Tất cả các Client gửi yêu cầu vào đây kèm theo định danh tiến trình `PID` của mình.
- **Hàng đợi Response riêng tư (`/shared_responses_<PID>`):** Mỗi Client khi khởi tạo sẽ tạo một queue riêng biệt gắn liền với PID. Server sau khi xử lý xong sẽ gửi trả kết quả chính xác vào queue của Client đó. Đảm bảo **tuyệt đối không xảy ra hiện tượng đọc chéo dữ liệu giữa các Client**.
- **Server đa luồng (Worker Thread Pool):** Listener thread nhận request từ queue và đẩy vào Job Queue nội bộ. Nhóm Worker Threads (dùng POSIX Threads, Mutex, Condition Variable) xử lý song song, giúp các tác vụ tính toán nặng không làm nghẽn toàn bộ hệ thống.

```text
 [Client 1 (PID 1001)] ──┐
 [Client 2 (PID 1002)] ──┼──> [/shared_requests] ──> [Server Listener Thread]
 [Client N (PID 2048)] ──┘                                   │
                                                     (Internal Job Queue)
                                                             │
                                                  [Worker Thread Pool]
                                                    ├── Worker 1 ──> [/shared_responses_1001]
                                                    ├── Worker 2 ──> [/shared_responses_1002]
                                                    └── Worker N ──> [/shared_responses_2048]
```

### 1.2. Các điểm sáng kỹ thuật giúp ghi điểm tuyệt đối (10/10)
1. **POSIX Message Priority (4 cấp độ):** Hỗ trợ `LOW (0)`, `NORMAL (10)`, `HIGH (20)`, `URGENT (30)`. Message ưu tiên cao sẽ vượt lên trước trong hàng đợi của Linux Kernel.
2. **Quản lý tài nguyên & Phòng chống Deadlock:** Áp dụng `mq_timedsend` và `mq_timedreceive` với cơ chế timeout micro/millisecond chính xác (`CLOCK_REALTIME`). Client và Server không bao giờ bị treo vĩnh viễn (hung process).
3. **Tiện ích độc lập dọn dẹp hàng đợi mồ côi (`bin/ipc_cleanup`):** Đọc trực tiếp Virtual Filesystem `/dev/mqueue` của Kernel để phát hiện và thu hồi các queue bị rò rỉ khi tiến trình bị tắt cưỡng bức (`kill -9`).
4. **Interactive REPL Shell:** Giao diện dòng lệnh chuyên nghiệp thời gian thực với màu sắc ANSI, hỗ trợ chuyển đổi độ ưu tiên linh hoạt.
5. **Công cụ đo tải chuyên nghiệp (`bin/benchmark`):** Sinh hàng chục tiến trình đồng thời bắn hàng ngàn request, thống kê Throughput (RPS), Latency Min/Max/Average/p95/p99.
6. **An toàn bộ nhớ và chuẩn công nghiệp:**
   - Biên dịch nghiêm ngặt: `-std=gnu11 -Wall -Wextra -Werror -pedantic`.
   - Vượt qua 100% các bài kiểm thử: AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), Valgrind Memcheck (0 leak, 0 error).

---

## 2. CHUẨN BỊ MÔI TRƯỜNG & BIÊN DỊCH TRƯỚC DEMO

Trước khi thầy gọi lên trình bày, hãy chuẩn bị sẵn sàng môi trường sạch:

```bash
# 1. Di chuyển vào thư mục dự án
cd ~/linux-shared-message-queues

# 2. Đảm bảo virtual filesystem /dev/mqueue đã được mount
ls -la /dev/mqueue

# 3. Dọn dẹp sạch sẽ các queue còn sót lại từ lần chạy trước
./bin/ipc_cleanup --all 2>/dev/null || true

# 4. Biên dịch sạch sẽ toàn bộ dự án
make clean && make
```

**Kết quả kỳ vọng:**
Lệnh `make` chạy qua các file và sinh đầy đủ các binary trong thư mục `bin/`:
- `bin/server`
- `bin/client`
- `bin/benchmark`
- `bin/ipc_cleanup`
- `bin/test_ipc_queue`
- `bin/test_protocol`
- `bin/raw_send`
Không xuất hiện bất kỳ dòng Warning hay Error nào!

---

## 3. KỊCH BẢN DEMO TRỰC QUAN TỪNG BƯỚC (LIVE DEMO GUIDE)

### Thiết lập không gian làm việc (Khuyên dùng 3 Terminal)
Chia màn hình thành 2 hoặc 3 cửa sổ Terminal cạnh nhau:
- **Terminal 1 (Trái):** Dành riêng cho `Server` (theo dõi log xử lý và worker threads).
- **Terminal 2 (Phải - Trên):** Dành cho `Client`, `REPL Shell`, `Benchmark`.
- **Terminal 3 (Phải - Dưới):** Dành cho kiểm tra `/dev/mqueue` và tiện ích `ipc_cleanup`.

---

### Bước 1: Khởi động Server & Kiến trúc Đa luồng

**Tại Terminal 1 (Server):**
```bash
./bin/server
```

**Thao tác giải thích với Thầy:**
- *"Thưa thầy, khi Server khởi động, tiến trình tạo hàng đợi tiếp nhận chung `/shared_requests` với quyền `0660`, dung lượng tối đa 10 tin nhắn, mỗi tin tối đa 256 bytes."*
- *"Server khởi tạo sẵn một Worker Pool gồm 4 worker threads bằng `pthread_create`. Listener thread sẽ thực hiện `mq_receive` liên tục, còn các worker thread ngủ trong trạng thái chờ biến điều kiện (`pthread_cond_wait`)."*

---

### Bước 2: Client One-shot & Các Command Cốt lõi

**Tại Terminal 2 (Client):** Thực hiện lần lượt các lệnh:

```bash
# 1. Kiểm tra kết nối cơ bản (Heartbeat)
./bin/client --cmd "PING"

# 2. Lấy thời gian hệ thống chuẩn ISO 8601
./bin/client --cmd "TIME"

# 3. Tính toán số học CPU-bound (Fibonacci)
./bin/client --cmd "FIB 15"

# 4. Xử lý chuỗi ký tự
./bin/client --cmd "UPPER xin chao he dieu hanh linux"
```

**Thao tác giải thích với Thầy:**
- Mở **Terminal 1** chỉ cho thầy thấy: Khi request đến, Listener thread tiếp nhận, đưa vào hàng đợi; một Worker thread rảnh lấy ra thực thi, đo thời gian xử lý chính xác đến millisecond, rồi gửi về queue `/shared_responses_<PID>` của Client.
- Client nhận được response, in ra màn hình trạng thái `OK`, thời gian thực thi (vd: `0.001 ms`) và kết quả. Sau đó Client tự động unlink queue của mình, không để lại rác trong Kernel.

---

### Bước 3: Các Dịch vụ Hệ thống Chuyên sâu (SYSINFO, STATS, CALC)

**Tại Terminal 2 (Client):**
```bash
# 1. Đọc thông số Linux Kernel trực tiếp từ /proc
./bin/client --cmd "SYSINFO"

# 2. Thống kê vận hành của Server
./bin/client --cmd "STATS"

# 3. Tính toán biểu thức số học
./bin/client --cmd "CALC 125 * 8"
```

**Thao tác giải thích với Thầy:**
- *"Lệnh `SYSINFO` chứng minh tính năng cấp hệ thống: Server trích xuất thông tin trực tiếp từ Virtual Filesystem `/proc/uptime`, `/proc/loadavg` và `/proc/meminfo` của Linux Kernel để gửi cho Client."*
- *"Lệnh `STATS` cung cấp observability: cho biết tổng số request đã xử lý, số request lỗi, số worker threads đang sẵn sàng và thời gian server đã chạy."*

---

### Bước 4: Trực quan hóa Cơ chế Độ ưu tiên (Message Priority)

**Tại Terminal 2 (Client):**
```bash
# Gửi tin nhắn với mức ưu tiên THẤP (LOW = 0)
./bin/client --cmd "UPPER do uu tien thap" --prio LOW

# Gửi tin nhắn với mức ưu tiên KHẨN CẤP (URGENT = 30)
./bin/client --cmd "UPPER do uu tien khan cap" --prio URGENT
```

**Thao tác giải thích với Thầy:**
- *"Hệ thống POSIX Message Queue hỗ trợ thuộc tính `msg_prio` nguyên bản ở tầng Kernel. Khi nhiều tin nhắn nằm cùng lúc trong `/shared_requests`, Linux Kernel dùng cấu trúc cây đỏ-đen (Red-Black Tree) hoặc danh sách ưu tiên để đảm bảo tin nhắn có priority cao hơn (URGENT=30) luôn được `mq_receive()` lấy ra trước các tin nhắn bình thường (NORMAL=10) hay thấp (LOW=0)."*

---

### Bước 5: Giao diện tương tác dòng lệnh trực tiếp (Interactive REPL Shell)

**Tại Terminal 2 (Client):**
```bash
# Khởi động REPL Shell (chạy không đối số)
./bin/client
```

Trong dấu nhắc lệnh `client-ipc> `, gõ lần lượt:
```text
client-ipc> help
client-ipc> PING
client-ipc> FIB 25
client-ipc> CALC 999 / 3
client-ipc> priority URGENT
client-ipc> UPPER test uu tien urgent
client-ipc> STATS
client-ipc> exit
```

**Thao tác giải thích với Thầy:**
- *"Ở chế độ REPL, Client duy trì phiên làm việc liên tục: hàng đợi response được giữ mở cho đến khi người dùng gõ `exit` hoặc `quit`."*
- *"Giao diện hỗ trợ lệnh `help`, hỗ trợ đổi mức độ ưu tiên trực tiếp bằng `priority <LEVEL>` và có mã màu ANSI trực quan (thành công màu xanh, lỗi màu đỏ)."*

---

### Bước 6: Đo tải & Đánh giá Hiệu năng (Benchmark High Concurrency)

Đây là phần **ấn tượng nhất** chứng minh hệ thống xử lý đa luồng hoạt động thực thụ!

**Tại Terminal 2 (Client):**
```bash
# 1. Thử nghiệm với 5 client đồng thời, mỗi client gửi 20 request (Tổng 100 requests)
./bin/benchmark --clients 5 --requests 20 --cmd PING --prio NORMAL

# 2. Thử nghiệm áp lực cao: 20 client đồng thời, mỗi client gửi 50 request (Tổng 1.000 requests)
./bin/benchmark --clients 20 --requests 50 --cmd PING --prio HIGH
```

**Thao tác giải thích với Thầy:**
- Chỉ vào bảng thống kê do `benchmark` in ra:
  - **Throughput (RPS):** Đạt hàng chục ngàn requests/giây (vượt trội so với socket truyền thống).
  - **Round-Trip Latency:** Hiển thị độ trễ tối thiểu, tối đa, trung bình và phân vị **p95, p99** tính bằng microsecond ($\mu s$).
  - **Tỷ lệ thành công:** `100% Successful Requests`, `0 Errors, 0 Timeouts`.
- *"Nhờ Worker Thread Pool, các client được phục vụ đồng thời mà không bị thắt cổ chai, và kiến trúc 1 request queue + N response queues xử lý trơn tru không tranh chấp kết quả."*

---

### Bước 7: Cơ chế Dọn dẹp Hàng đợi Mồ côi (Orphaned Queue Cleanup)

Giảng viên môn Hệ điều hành rất hay hỏi: *"Nếu tiến trình Client hoặc Server bị crash (kill -9) giữa chừng thì queue có mất không? Làm sao dọn?"*

**Tại Terminal 3:**
```bash
# 1. Liệt kê các queue đang hoạt động trên hệ thống
./bin/ipc_cleanup --list
# Hoặc kiểm tra trực tiếp VFS:
ls -l /dev/mqueue
```

**Kịch bản mô phỏng sự cố (Chaos Demonstration):**
1. Bật một client REPL ở Terminal 2:
   ```bash
   ./bin/client
   ```
2. Mở Terminal 3, tìm PID của client vừa mở rồi bắn tín hiệu cưỡng bức:
   ```bash
   pkill -9 -f "bin/client"
   ```
3. Kiểm tra lại `/dev/mqueue`:
   ```bash
   ./bin/ipc_cleanup --list
   ```
   *(Sẽ thấy hàng đợi mồ côi `/shared_responses_<PID>` vẫn tồn tại do `kill -9` không cho tiến trình chạy hàm dọn dẹp).*
4. Thực hiện dọn dẹp tự động bằng tiện ích của nhóm:
   ```bash
   ./bin/ipc_cleanup --all
   ./bin/ipc_cleanup --list
   ```
   *(Toàn bộ queue rác được dọn dẹp sạch sẽ 100%).*

---

### Bước 8: Kiểm định Chất lượng Phần mềm (Unit Test, Integration, Sanitizers)

Chứng minh mã nguồn được xây dựng bài bản theo chuẩn công nghiệp:

```bash
# 1. Chạy toàn bộ Unit Tests (Core IPC + Protocol/Worker Pool)
make test-unit

# 2. Chạy kiểm thử an toàn bộ nhớ AddressSanitizer & UndefinedBehaviorSanitizer
make sanitize

# 3. Chạy kiểm tra rò rỉ bộ nhớ với Valgrind (nếu máy có cài Valgrind)
make valgrind
```

**Điểm nhấn kết quả:**
- Unit Test: **44/44 Test IPC Core PASSED**, **52/52 Test Protocol & Worker Pool PASSED**.
- Valgrind: **0 errors from 0 contexts, no leaks are possible**.
- AddressSanitizer: Không có heap-buffer-overflow, không stack-use-after-return.

---

### Bước 9: Script Demo Tự động Toàn diện

Nếu thời gian của buổi bảo vệ có hạn (chỉ có 3 - 5 phút để demo), nhóm có thể chạy ngay script tự động:

```bash
# Đảm bảo server đang chạy ở Terminal 1, sau đó chạy:
./demo/demo.sh
```
Script sẽ tự động chạy 8 bài demo từ PING, FIB, UPPER, URGENT Priority đến Benchmark tải đa tiến trình và báo `AUTOMATED DEMO COMPLETED SUCCESSFULLY`.

Cuối cùng, quay lại Terminal 1 và bấm `Ctrl+C` trên Server để thầy thấy **Graceful Shutdown**:
- Server bắt tín hiệu `SIGINT`.
- Đợi các worker thread xả hết việc đang làm (drain worker threads).
- Hủy hàng đợi `/shared_requests`.
- Thoát sạch sẽ không rò rỉ tài nguyên.

---

## 4. KỊCH BẢN THUYẾT TRÌNH BÁO CÁO (ORAL PRESENTATION)

Thời lượng khuyến nghị: **10 - 12 phút trình bày + 5 phút hỏi đáp**.

### Phân chia phần thuyết trình cho 4 thành viên

#### Thành viên 1: Kiến trúc Lõi POSIX Message Queue & Quản trị Tài nguyên
- **Thời lượng:** ~2.5 phút.
- **Nội dung nói:**
  - Giới thiệu tổng quan về đề tài: Cơ chế IPC bằng POSIX Message Queue trên Linux.
  - Phân tích bản chất của POSIX Message Queue: Được quản lý trực tiếp bởi Linux Kernel, xuất hiện trong hệ thống tập tin ảo `/dev/mqueue`.
  - Thiết kế thư viện bọc an toàn `ipc_queue.c`: Xử lý các cờ `O_CREAT`, `O_EXCL`, `O_NONBLOCK`.
  - Cơ chế Timeout (`mq_timedsend`, `mq_timedreceive`) dựa trên `CLOCK_REALTIME` để ngăn chặn deadlock.
  - Vấn đề hàng đợi mồ côi khi tiến trình crash và giải pháp công cụ `ipc_cleanup`.

#### Thành viên 2: Kiến trúc Server Đa Luồng (Worker Thread Pool) & Protocol
- **Thời lượng:** ~3 phút.
- **Nội dung nói:**
  - Giải thích điểm nghẽn của mô hình Server đơn luồng truyền thống.
  - Trình bày kiến trúc **Multi-threaded Worker Pool**:
    + *Listener Thread:* Chuyên trách `mq_receive` từ `/shared_requests` rồi đẩy vào Job Queue nội bộ.
    + *Worker Threads:* Chờ việc qua `pthread_cond_wait`, lấy request, xử lý độc lập và gửi kết quả về `/shared_responses_<PID>`.
  - Thiết kế Protocol chuẩn hóa: Phân tích cú pháp `<PID>|<CMD>|<ARG>`, phòng chống Buffer Overflow và Injection.
  - Các dịch vụ hệ thống triển khai: `SYSINFO` (đọc `/proc`), `FIB`, `TIME`, `STATS`, `CALC`.
  - Cơ chế Graceful Shutdown khi bắt tín hiệu `SIGINT` (Ctrl+C).

#### Thành viên 3: Hệ Sinh Thái Client, Interactive REPL & Đánh Giá Hiệu Năng
- **Thời lượng:** ~3 phút.
- **Nội dung nói:**
  - Giới thiệu 2 chế độ Client: One-shot CLI (cho automation/scripting) và Interactive REPL Shell (cho người dùng tương tác).
  - Trực tiếp thao tác và giải thích phần Live Demo (Bước 2 đến Bước 6).
  - Phân tích kết quả Benchmark đo tải:
    + Thí nghiệm với 1, 5, 10, 20 tiến trình Client đồng thời.
    + Phân tích Throughput và các mốc Latency p95, p99.
    + Khẳng định kiến trúc đảm bảo Client A không bao giờ đọc nhầm response của Client B.

#### Thành viên 4: Build System, Kiểm Thử Toàn Diện & So Sánh Kỹ Thuật
- **Thời lượng:** ~2.5 phút.
- **Nội dung nói:**
  - Trình bày hệ thống Build Makefile: Tự động hóa phụ thuộc header (`-MMD -MP`), cờ biên dịch nghiêm ngặt `-Werror`.
  - Báo cáo kết quả kiểm thử chất lượng:
    + Unit tests (96 test cases passed 100%).
    + Integration tests & Chaos tests (kiểm tra ngoại lệ tải nặng, chuỗi độc hại).
    + Memory profiling: Chứng minh 0 memory leak qua Valgrind và AddressSanitizer.
  - Bảng so sánh kỹ thuật: POSIX Message Queue so với Pipe, FIFO, UNIX Domain Socket và Shared Memory.

---

### Kịch bản báo cáo ngắn gọn (Dành cho 1 người đại diện)
*(Nếu nhóm chỉ có 1 bạn đại diện thuyết trình toàn bộ)*:
1. **Mở đầu (1 phút):** Chào thầy, giới thiệu đề tài, mục tiêu mô phỏng giao tiếp đa tiến trình bằng POSIX Message Queue theo mô hình 1 Request Queue chung và N Response Queues riêng.
2. **Kiến trúc & Kỹ thuật (3 phút):** Nêu bật 3 trụ cột: Lớp trừu tượng `ipc_queue`, Server đa luồng với Worker Thread Pool, và Client REPL/Benchmark.
3. **Demo Trực quan (4 phút):** Chạy `server`, chạy các lệnh `PING`, `SYSINFO`, chạy REPL shell, chạy `benchmark 10 clients`, và biểu diễn `ipc_cleanup`.
4. **Kiểm định & Kết luận (2 phút):** Trình chiếu kết quả `make test-unit`, Valgrind không rò rỉ bộ nhớ, tóm tắt bài học kinh nghiệm và sẵn sàng trả lời phản biện.

---

## 5. BỘ CÂU HỎI VẤN ĐÁP THƯỜNG GẶP (Q&A CHEAT SHEET)

Dưới đây là các câu hỏi giảng viên chuyên ngành Hệ điều hành / Lập trình hệ thống thường đặt ra nhất, kèm theo câu trả lời kỹ thuật chuẩn xác:

---

### Câu 1: Tại sao nhóm chọn POSIX Message Queue mà không dùng UNIX Domain Socket hay Shared Memory? Đánh đổi là gì?
- **Trả lời:**
  - **So với Pipe/FIFO:** Pipe/FIFO chỉ là luồng byte phi cấu trúc (byte stream), người lập trình phải tự phân tách ranh giới gói tin. Message Queue phân định ranh giới thông điệp tự nhiên (message boundary) và hỗ trợ sẵn **Message Priority** ở tầng kernel.
  - **So với UNIX Domain Socket:** Socket rất mạnh nhưng đi kèm chi phí giao thức và quản lý connection (accept/connect/epoll). Message Queue là mô hình phi kết nối hướng thông điệp (connectionless message-oriented), cực kỳ phù hợp cho mô hình Request/Response ngắn gọn giữa các tiến trình local.
  - **So với Shared Memory:** Shared Memory có tốc độ nhanh nhất nhưng không có cơ chế đồng bộ hóa; buộc lập trình viên phải tự quản lý Semaphore/Mutex rất dễ gây Race Condition hoặc Deadlock. Message Queue do Kernel đồng bộ hóa sẵn (thread-safe, process-safe).
  - **Đánh đổi:** Message Queue bị giới hạn bởi kích thước thông điệp (`msg_max` thường là 10, `msgsize_max` thường là 8KB mặc định của Linux). Do đó nó tối ưu cho tin nhắn điều khiển, không phù hợp để truyền file dung lượng lớn hàng trăm Megabyte.

---

### Câu 2: Làm thế nào đảm bảo Client A không bao giờ nhận nhầm kết quả của Client B?
- **Trả lời:**
  - Nhóm áp dụng mô hình **Queue-per-Client**: Mỗi Client khi khởi động sẽ lấy `PID` của chính nó thông qua `getpid()` và tạo riêng một queue có tên `/shared_responses_<PID>` với cờ `O_CREAT | O_EXCL`.
  - Trong gói tin Request gửi lên `/shared_requests`, trường đầu tiên luôn là `<CLIENT_PID>`.
  - Khi Server xử lý xong, nó trích xuất `<CLIENT_PID>` và mở đúng queue `/shared_responses_<PID>` để gửi kết quả về. Vì mỗi Client chỉ lắng nghe trên queue mang PID của riêng nó, việc nhận chéo dữ liệu là **bất khả thi về mặt kiến trúc**.

---

### Câu 3: Khi tiến trình bị `kill -9`, tại sao Message Queue vẫn còn trong hệ thống? Nhóm giải quyết thế nào?
- **Trả lời:**
  - POSIX Message Queue có tính chất **Kernel Persistence** (sống theo vòng đời của Hệ điều hành), không phải Process Persistence. Queue chỉ bị xóa khi có tiến trình gọi `mq_unlink()` hoặc khi máy khởi động lại (hoặc unmount `/dev/mqueue`).
  - Khi bị `kill -9` (`SIGKILL`), tiến trình bị dừng ngay lập tức bởi Kernel, không có cơ hội thực thi các hàm dọn dẹp (signal handler không thể bắt được `SIGKILL`).
  - **Giải pháp của nhóm:** Nhóm đã phát triển riêng công cụ `bin/ipc_cleanup`. Tiện ích này mở và quét Virtual Filesystem `/dev/mqueue`, nhận diện các queue có tiền tố của dự án (`/shared_*`) và tiến hành gọi `mq_unlink` để giải phóng triệt để tài nguyên Kernel.

---

### Câu 4: Message Priority trong POSIX Message Queue hoạt động như thế nào trong Linux Kernel?
- **Trả lời:**
  - Trong hàm `mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio)`, tham số `msg_prio` quy định độ ưu tiên của tin nhắn.
  - Bên trong Linux Kernel, hàng đợi tin nhắn không phải là một mảng FIFO thuần túy. Kernel lưu trữ các message theo thứ tự giảm dần của priority. Các message có cùng priority mới tuân theo thứ tự FIFO.
  - Do đó, khi gọi `mq_receive()`, tin nhắn có độ ưu tiên cao nhất trong queue sẽ luôn được lấy ra đầu tiên, ngay cả khi nó được gửi sau các tin nhắn có độ ưu tiên thấp hơn.

---

### Câu 5: Nếu 100 client cùng gửi lệnh tính toán nặng (`FIB 45`) thì Server xử lý ra sao để không bị sập?
- **Trả lời:**
  - Server sử dụng mô hình **Worker Thread Pool**:
    1. Hàng đợi `/shared_requests` có dung lượng đệm (mặc định 10 message).
    2. Listener Thread chỉ làm một nhiệm vụ duy nhất là bốc message từ queue và đưa vào Internal Job Queue của Worker Pool, thời gian chiếm dụng cực ngắn.
    3. Các Worker Threads lấy việc ra tính toán song song trên các nhân CPU khác nhau.
    4. Nếu hàng đợi đầy, các Client tiếp theo sẽ bị chặn lại hoặc chờ theo cơ chế `mq_timedsend` có timeout. Nếu vượt quá timeout (3 giây), Client sẽ báo lỗi an toàn thay vì làm sập hệ thống.

---

### Câu 6: Làm thế nào nhóm phòng tránh lỗi Buffer Overflow và Null-Byte Injection?
- **Trả lời:**
  - Ở lớp IPC Core, hàm `ipc_queue_receive()` kiểm tra nghiêm ngặt: kích thước buffer của bên nhận bắt buộc phải lớn hơn hoặc bằng thuộc tính `mq_msgsize` của queue (256 bytes), nếu không sẽ từ chối nhận để tránh tràn bộ đệm.
  - Sau khi nhận dữ liệu thô từ Kernel, thư viện luôn chủ động gán byte kết thúc chuỗi `\0` tại vị trí `received_bytes`.
  - Bộ parser trong `protocol.c` kiểm tra tính hợp lệ của định dạng: từ chối các chuỗi không đủ trường, từ chối payload chứa ký tự phân cách `|`, và kiểm tra không có ký tự null nằm giữa thông điệp trước khi xử lý.

---

## 6. BẢNG TỔNG HỢP CÁC LỆNH NHANH CHO BUỔI BẢO VỆ

Hãy in hoặc lưu bảng này để tra cứu nhanh khi đang đứng trước hội đồng:

| Mục đích | Lệnh thực thi | Ghi chú |
|---|---|---|
| **Dọn dẹp sạch sẽ** | `./bin/ipc_cleanup --all` | Xóa sạch queue rác trước khi demo |
| **Biên dịch toàn bộ** | `make clean && make` | Đảm bảo code mới nhất, 0 warning |
| **Bật Server** | `./bin/server` | Mở ở Terminal 1 |
| **Client PING** | `./bin/client --cmd "PING"` | Test kết nối cơ bản |
| **Client Fibonacci** | `./bin/client --cmd "FIB 20" --prio HIGH` | Test tính toán và priority |
| **Client Sysinfo** | `./bin/client --cmd "SYSINFO"` | Đọc dữ liệu Kernel `/proc` |
| **Client REPL Shell** | `./bin/client` | Chế độ tương tác nhập lệnh liên tục |
| **Đo tải Benchmark** | `./bin/benchmark --clients 10 --requests 50` | Đo Throughput và Latency p95/p99 |
| **Xem queue hệ thống** | `./bin/ipc_cleanup --list` | Soi trực tiếp `/dev/mqueue` |
| **Demo tự động** | `./demo/demo.sh` | Chạy toàn bộ 8 bài test demo |
| **Chạy Unit Test** | `make test-unit` | 96/96 tests passed 100% |
| **Kiểm tra Memory Leak** | `make valgrind` | Chứng minh 0 memory leaks |

---

> **Lời chúc:** Chúc nhóm bạn tự tin, phối hợp nhịp nhàng và đạt điểm số tối đa (10/10) trong buổi bảo vệ đồ án!

