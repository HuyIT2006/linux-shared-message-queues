# Kế hoạch và phân công nhiệm vụ dự án

# Shared Message Queues trong Linux IPC bằng ngôn ngữ C

## 1. Thông tin chung dự án

### 1.1. Mục tiêu

Xây dựng một chương trình mô phỏng giao tiếp liên tiến trình bằng **POSIX Shared Message Queues** trên Linux.

Hệ thống gồm:

- Một tiến trình server nhận yêu cầu.
- Nhiều tiến trình client gửi yêu cầu đồng thời.
- Một queue dùng chung để nhận request.
- Một queue riêng cho mỗi client để nhận response.

Sau khi hoàn thành, cần chạy được demo và giải thích được:

- Hàng đợi dùng chung hoạt động như thế nào.
- Client và server trao đổi dữ liệu ra sao.
- Tính độc lập của từng tiến trình.
- Các lỗi thường gặp trong IPC.
- Cách dọn dẹp queue khi kết thúc chương trình.

### 1.2. Công cụ thống nhất

| Hạng mục | Công cụ hoặc quy ước |
|---|---|
| Ngôn ngữ lập trình | C chuẩn POSIX |
| Bộ biên dịch | GCC |
| Build project | Make |
| Hệ điều hành | Linux hoặc WSL trên Windows |
| IPC được sử dụng | POSIX Message Queue |
| Trình quản lý mã nguồn | Git và GitHub |
| Header API | `sys/stat.h`, `mqueue.h`, `fcntl.h`, `errno.h` |
| Chỉnh sửa mã nguồn | VS Code |
| Kiểm thử | Test C và shell script |

Thông tin build tham khảo:

```bash
make
./bin/server
./bin/client PING
```

Khi cần liên kết thư viện POSIX thực thời:

```makefile
LDLIBS += -lrt
```

Trên môi trường Linux hiện đại có thể thư viện chỉ cần thiết tùy phiên bản GCC, nhưng vẫn phải giữ `-lrt` trong Makefile để demo nhất quán trên nhiều phiên bản.

### 1.3. Quy ước IPC

Dự án sử dụng POSIX Message Queue, không sử dụng System V Message Queue.

Các queue đề xuất:

```text
/shared_requests       Queue dùng chung để gửi yêu cầu
/shared_responses_PID  Queue riêng cho client có process ID tương ứng
```

Ví dụ với client có PID là `1234`:

```text
/shared_responses_1234
```

Các thuộc tính mặc định đề xuất:

```text
Queue request:   /shared_requests
Số message:      10
Kích thước:      256 byte
Client response: /shared_responses_<PID>
Số message:      10
Kích thước:      256 byte
```

Các API bắt buộc:

```c
mq_open();
mq_send();
mq_receive();
mq_getattr();
mq_close();
mq_unlink();
```

## 2. Kiến trúc tổng quan

```text
+------------+                              +------------+
| Client A   |                              |            |
| PID 1001   | ---- request --------------> |            |
+------------+                              |            |
                                            |   Server   |
+------------+    /shared_requests          |  Nhận, xử lý|
| Client B   | ---------------------------->|  và gửi     |
| PID 1002   |                              |            |
+------------+                              +------------+
                                            |
                                            | response
                                            v
                                /shared_responses_<PID>
```

Luồng hoạt động:

1. Client tạo queue response riêng bằng tên chứa PID.
2. Client gửi request đến `/shared_requests`.
3. Server nhận request và phân tích nội dung.
4. Server xử lý command.
5. Server gửi response vào queue response của client tương ứng.
6. Client nhận response và hiển thị kết quả.
7. Khi kết thúc, client đóng và xóa queue của mình.
8. Khi server kết thúc, server đóng và xóa `/shared_requests`.

## 3. Định nghĩa giao tiếp giữa client và server

Mỗi thành viên phải tuân thủ protocol chung dưới đây để tránh code không tương thích.

### 3.1. Request

Định dạng:

```text
<PID>|<COMMAND>|<ARGUMENT>
``>

Ví dụ:

```text
1001|PING|
1001|TIME|
1002|FIB|10
1003|UPPER|xin-chao
```

Các command dự kiến:

| Command | Argument | Ví dụ request | Response mong đợi |
|---|---|---|---|
| PING | Không có | `1001\|PING\|` | `PONG` |
| TIME | Không có | `1001\|TIME\|` | Thời gian server |
| FIB | Một số nguyên không âm | `1002\|FIB\|10` | `55` |
| UPPER | Chuỗi ký tự không chứa dấu `\|` | `1003\|UPPER\|xin-chao` | `XIN-CHAO` |
| UNKNOWN | Không áp dụng | Command không tồn tại | `ERROR: UNKNOWN_COMMAND` |

### 3.2. Response

Định dạng:

```text
<STATUS>|<MESSAGE>
```

Ví dụ:

```text
OK|PONG
OK|55
OK|XIN-CHAO
ERROR|UNKNOWN_COMMAND
ERROR|INVALID_ARGUMENT
```

Response phải được gửi đến đúng queue sau:

```text
/shared_responses_<PID>
```

### 3.3. Quy tắc xử lý dữ liệu

- Message tối đa 256 byte.
- Không chứa byte kết thúc `NUL` trừ khi cần cho chuỗi C.
- Luôn thêm ký tự kết thúc chuỗi sau `mq_receive()`:

```c
buffer[bytes_received] = '\0';
```

- Đảm bảo kết quả không vượt `MAX_MSG_SIZE`.
- Không dùng dấu `|` trong argument.
- Khi nhận message không hợp lệ, server trả về `ERROR|INVALID_REQUEST` thay vì crash.
- Khi queue chưa tồn tại, client không được gửi request. Client phải in thông báo rõ ràng hoặc chờ server khởi động.

## 4. Phân chia công việc

Dự án có 4 thành viên. Mỗi người có một phần trách nhiệm chính, nhưng tất cả đều phải phối hợp qua header protocol chung và review mã của nhau.

## Thành viên 1 — Owner IPC Core và Queue Wrapper

### Mục tiêu vai trò

Xây dựng tầng nền tảng thao tác POSIX Message Queue. Các thành viên khác không được tự viết lại các API queue một cách rời rạc; phải sử dụng module này.

### Công việc cần làm

- Viết `include/mqueue.h`.
- Viết `src/mqueue.c`.
- Khởi tạo queue bằng `mq_open()`.
- Mở queue request và queue response.
- Tạo queue không blocking hoặc blocking theo mục đích demo.
- Gửi và nhận message thông qua các wrapper.
- Đóng queue bằng `mq_close()`.
- Hủy queue bằng `mq_unlink()`.
- Xử lý lỗi từ `errno`.
- Kiểm tra thuộc tính queue bằng `mq_getattr()`.

### Hàm đề xuất

```c
mqd_t mq_open_queue(const char *name, int flags, mode_t mode,
                    const struct mq_attr *attr);

int mq_send_message(mqd_t queue, const char *message,
                    size_t length, unsigned int priority);

ssize_t mq_receive_message(mqd_t queue, char *buffer,
                           size_t buffer_size, unsigned int *priority);

int mq_close_queue(mqd_t queue);
int mq_unlink_queue(const char *name);
```

### Trường hợp lỗi cần xử lý

- Queue không tồn tại: `ENOENT`.
- Queue đã tồn tại: `EEXIST`.
- Queue đầy: `EAGAIN` hoặc timeout.
- Message quá dài: `EMSGSIZE`.
- Tham số không hợp lệ: `EINVAL`.
- Không có quyền truy cập: `EACCES`.

### Sản phẩm đầu ra

- Module queue hoạt động.
- Ví dụ tối thiểu gửi và nhận một message.
- Test cho queue tạo mới, gửi, nhận và xóa queue.
- Ghi file lỗi theo kiểu thống nhất để debug.

### Definition of Done

Vai trò này hoàn thành khi:

- Server hoặc client có thể gọi module mà không thao tác trực tiếp các API queue trùng lặp.
- Queue tạo mới hoặc mở queue hiện có đều đúng.
- Không để queue tồn tại sau chương trình server kết thúc.
- Các message quá dài hoặc queue đầy không làm crash chương trình.

## Thành viên 2 — Server và Protocol Processing

### Mục tiêu vai trò

Viết tiến trình server nhận request từ nhiều client và gửi response về queue riêng của từng client.

### Công việc cần làm

- Viết `src/server.c`.
- Viết `include/protocol.h`.
- Đăng ký signal để server dừng an toàn bằng `Ctrl+C`.
- Mở hoặc tạo `/shared_requests`.
- Chạy vòng lặp nhận message liên tục.
- Xác thực độ dài và định dạng request.
- Phân tích request theo dấu `|`.
- Thực hiện các command `PING`, `TIME`, `FIB`, `UPPER`.
- Xác định queue response từ PID trong request.
- Gửi response vào đúng queue của client.
- Trả về message lỗi khi command hoặc argument sai.
- Xử lý nhiều client liên tiếp hoặc đồng thời.

### Hàm đề xuất

```c
int parse_request(const char *input, Request *request);
int execute_command(const Request *request, char *response,
                    size_t response_size);
int send_response_to_client(pid_t client_pid,
                            const char *response);
```

### Sản phẩm đầu ra

- Server có thể nhận message liên tục.
- Server xử lý đầy đủ ít nhất bốn command.
- Server không crash khi nhận message sai định dạng.
- Server in ra log cho từng request và response.
- Server xóa queue request khi kết thúc.

### Definition of Done

Vai trò này hoàn thành khi:

- Client gửi `PING` nhận được `OK|PONG`.
- Hai client gửi request cùng lúc nhận được response ở queue riêng.
- `FIB` được tính đúng cho ít nhất các giá trị test đã thống nhất.
- Request sai không làm server dừng.
- Dừng server bằng `Ctrl+C` sẽ đóng queue sạch sẽ.

## Thành viên 3 — Client và Demo Script

### Mục tiêu vai trò

Viết chương trình client và kịch bản demo để người xem dễ hiểu toàn bộ luồng IPC.

### Công việc cần làm

- Viết `src/client.c`.
- Nhận tham số command từ dòng lệnh.
- Tạo queue response theo PID:

```bash
/shared_responses_<PID>
```

- Tạo queue bằng `mq_open()` trong chế độ tạo mới.
- Kiểm tra server đã chạy hay chưa thông qua queue request.
- Gửi request theo protocol chung.
- Chờ và nhận response bằng timeout hoặc blocking mode.
- In PID, queue name, request và response.
- Đóng và xóa queue response khi kết thúc.
- Viết `demo/demo.sh` để:
  - Chạy nhiều client cùng lúc.
  - Demo từng command.
  - Mô phỏng stress test.

### Ví dụ dòng lệnh

```bash
./bin/client PING
./bin/client TIME
./bin/client FIB 10
./bin/client UPPER xin-chao
```

### Demo nhiều client

```bash
./bin/client PING &
./bin/client FIB 10 &
./bin/client UPPER linux-ipc &
wait
```

### Demo stress test

Demo script có thể chạy một client nhiều lần hoặc một client gửi nhiều message liên tiếp.

```bash
for i in $(seq 1 20); do
    ./bin/client FIB 10 &
done
wait
```

### Sản phẩm đầu ra

- Client chạy được bằng command-line.
- Log demo dễ đọc.
- Script demo chạy được trên Linux/WSL.
- Client không để lại queue response sau khi kết thúc.
- Demo chạy được nhiều client mà response không bị lẫn giữa các client.

### Definition of Done

Vai trò này hoàn thành khi:

- Client gửi đúng định dạng request.
- Response luôn nhận từ queue của chính client.
- Demo một người xem chỉ cần chạy tối đa hai lệnh: server và script demo.
- Không để process chạy ngầm sau khi demo kết thúc.

## Thành viên 4 — Build, Test và Tài liệu

### Mục tiêu vai trò

Giúp dự án build thành công từ mã nguồn mới, có kiểm thử, tài liệu rõ ràng và có thể demo trước lớp.

### Công việc cần làm

#### Makefile và build

- Viết `Makefile`.
- Build thư mục `bin/` và `obj/`.
- Tạo target tối thiểu:

```makefile
all
server
client
clean
test
run-server
run-demo
```

- Liên kết thư mục `include/`.
- Thêm `-Wall -Wextra -Werror -pedantic` trong quá trình kiểm thử.
- Đảm bảo build sạch bằng:

```bash
make clean
make
```

#### Kiểm thử

Viết test cho các phần sau:

- Queue có thể tạo và mở lại.
- Gửi một message và nhận đúng nội dung.
- Xử lý queue đầy.
- Queue response khác nhau không nhận nhầm message.
- `PING` trả về `PONG`.
- `FIB 0`, `FIB 10`, `FIB 20` đúng.
- `UPPER` chuyển chuỗi sang chữ hoa.
- Command hoặc argument sai trả về lỗi.
- Message vượt kích thước không làm crash.
- Server có thể nhận nhiều client.

Có thể viết test bằng shell:

```bash
./tests/test_queue.sh
./tests/test_protocol.sh
./tests/test_integration.sh
```

#### Kiểm tra lỗi

Nếu môi trường hỗ trợ Valgrind, chạy:

```bash
valgrind --leak-check=full ./bin/server
valgrind --leak-check=full ./bin/client PING
```

Không cần sửa lỗi ngoài phạm vi dự án nếu không liên quan đến IPC.

#### Tài liệu

- Viết hoặc cập nhật `README.md`.
- Viết hướng dẫn chạy trên Linux và WSL.
- Giải thích kiến trúc hệ thống bằng sơ đồ.
- Giải thích từng API POSIX Message Queue.
- Liệt kê lỗi thường gặp và cách xử lý.
- Chuẩn bị slide hoặc nội dung thuyết trình.
- Chụp lại log kết quả demo.

#### Sản phẩm đầu ra

- Project build bằng một lệnh.
- Script test chạy lại được.
- README đủ để thành viên mới chạy dự án.
- Slide demo có đầy đủ sơ đồ và kết quả kiểm thử.
- Danh sách các queue tồn tại sau demo.

### Definition of Done

Vai trò này hoàn thành khi:

- `make clean && make` chạy thành công.
- Không có warning nghiêm trọng trong build.
- Một người mới clone repository có thể chạy theo README.
- Script test có kết quả PASS hoặc danh sách lỗi cụ thể.
- Không còn queue test được tạo sau khi kết thúc demo.

## 5. Cấu trúc thư mục dự án

```text
linux-shared-message-queues/
├── Makefile
├── README.md
├── PHAN_CONG_NHIEM_VU.md
├── include/
│   ├── mqueue.h
│   └── protocol.h
├── src/
│   ├── mqueue.c
│   ├── server.c
│   └── client.c
├── demo/
│   └── demo.sh
└── tests/
    ├── test_mqueue.c
    ├── test_protocol.c
    └── test_integration.sh
```

Không bắt buộc dùng đúng tên này nếu cả nhóm thống nhất tên khác. Tuy nhiên mỗi thư mục phải có vai trò rõ ràng và không đặt toàn bộ mã vào một file duy nhất.

## 6. Thứ tự thực hiện cho cả nhóm

### Giai đoạn 1 — Khởi tạo dự án

Người phụ trách chính: Thành viên 4 phối hợp cả nhóm.

- Tạo cấu trúc thư mục.
- Viết Makefile đầu tiên.
- Thêm file header chung.
- Thống nhất tên queue và kích thước message.
- Tạo nhánh Git riêng cho từng thành viên.

Mốc hoàn thành:

```text
make thành công và nhóm có thể include các header chung.
```

### Giai đoạn 2 — Queue core và protocol

Người phụ trách chính: Thành viên 1 và Thành viên 2.

- Hoàn thiện tạo, gửi, nhận và xóa queue.
- Định nghĩa cấu trúc Request và Response.
- Viết parser đơn giản.

Mốc hoàn thành:

```text
Một chương trình gửi được một message và server nhận được message đó.
```

### Giai đoạn 3 — Server và client

Người phụ trách chính: Thành viên 2 và Thành viên 3.

- Ghép server với queue wrapper.
- Ghép client với queue wrapper.
- Gửi `PING` và nhận `PONG`.
- Thêm các command còn lại.

Mốc hoàn thành:

```text
Client nhận đúng response từ queue của chính client.
```

### Giai đoạn 4 — Demo và kiểm thử

Người phụ trách chính: Thành viên 3 và Thành viên 4.

- Viết demo script.
- Thêm stress test.
- Kiểm tra nhiều client đồng thời.
- Viết tài liệu và slide.

Mốc hoàn thành:

```text
Demo chạy ổn định, không còn queue rác và kết quả có thể minh họa.
```

## 7. Quy trình làm việc Git

### Nhánh chính

```text
main
```

### Nhánh cá nhân

```text
feature/tv1-mqueue-core
feature/tv2-server-protocol
feature/tv3-client-demo
feature/tv4-build-test-docs
```

Không commit trực tiếp vào `main`.

### Quy trình thay đổi code

1. Cập nhật `main` mới nhất.
2. Tạo nhánh feature.
3. Tự test phần mình làm.
4. Commit code rõ ràng.
5. Tạo Pull Request.
6. Ít nhất một thành viên review.
7. Thành viên phụ trách liên quan xác nhận không phá đổi protocol.
8. Merge sau khi CI hoặc build thủ công thành công.

### Ví dụ commit

```text
feat: add POSIX message queue wrapper
feat: implement FIB and UPPER server commands
feat: add multi-client demo script
test: add message queue integration test
docs: update Linux IPC demo instructions
fix: handle queue full error in client
```

## 8. Nội quy khi thay đổi protocol

Header `protocol.h`, định dạng request và response chỉ được thay đổi sau khi thống nhất nhóm.

Trước khi sửa protocol, thành viên phải:

- Thông báo trong GitHub Issue hoặc nhóm chat của dự án.
- Cập nhật file tài liệu này.
- Thêm hoặc sửa test liên quan.
- Đồng thời cập nhật server và client.
- Không thay đổi protocol chỉ trên server hoặc client.

Nếu protocol thay đổi sau khi client đã chạy, phải giữ tương thích ngược hoặc nâng phiên bản rõ ràng, ví dụ:

```text
PROTOCOL_VERSION = 1
```

## 9. Danh sách test bắt buộc

| Test | Người thực hiện | Người hỗ trợ | Kết quả mong đợi |
|---|---|---|---|
| Tạo và xóa queue | Thành viên 1 | Thành viên 4 | Queue không còn sau khi kết thúc |
| Gửi và nhận một message | Thành viên 1 | Thành viên 2 | Nội dung nhận chính xác |
| Server nhận PING | Thành viên 2 | Thành viên 3 | Trả về `OK|PONG` |
| Client PING | Thành viên 3 | Thành viên 2 | Client nhận đúng response |
| Hai client đồng thời | Thành viên 3 | Thành viên 2, 4 | Response không bị lẫn client |
| Command FIB | Thành viên 2 | Thành viên 4 | Kết quả đúng với một số giá trị |
| Command UPPER | Thành viên 2 | Thành viên 4 | Kết quả đúng |
| Command sai | Thành viên 2 | Thành viên 4 | Trả về message lỗi, server vẫn chạy |
| Message quá dài | Thành viên 1 | Thành viên 4 | Không crash hoặc ghi đè bộ nhớ |
| Queue đầy | Thành viên 1 | Thành viên 2, 3 | Có timeout hoặc xử lý `EAGAIN` |
| Stress 100 message | Thành viên 3 | Thành viên 4 | Không mất hoặc lẫn message |
| Build sạch | Thành viên 4 | Cả nhóm | Build thành công từ `make clean` |

## 10. Checklist trước buổi demo

### Kiểm tra build

- [ ] `make clean` chạy được.
- [ ] `make` chạy được trên máy demo.
- [ ] Không còn file build trong `git`.
- [ ] README có đúng câu lệnh demo.

### Kiểm tra server

- [ ] Server khởi động đúng.
- [ ] `/shared_requests` được tạo.
- [ ] Server in log theo request.
- [ ] Dừng server bằng `Ctrl+C` dọn dẹp queue.

### Kiểm tra client

- [ ] Client PING hoạt động.
- [ ] Client TIME hoạt động.
- [ ] Client FIB hoạt động.
- [ ] Client UPPER hoạt động.
- [ ] Hai client chạy đồng thời không nhận nhầm response.
- [ ] Client không để lại queue response sau khi kết thúc.

### Kiểm tra tài liệu

- [ ] README mô tả cách chạy trên Linux/WSL.
- [ ] Slide có kiến trúc hệ thống.
- [ ] Có hình ảnh hoặc log kết quả thực tế.
- [ ] Mỗi thành viên biết phần mình thuyết trình.
- [ ] Nhóm giải thích được điểm khác giữa POSIX và System V message queue.

## 11. Nhiệm vụ thuyết trình cho từng thành viên

### Thành viên 1

Thuyết trình:

- POSIX Message Queue là gì.
- Các API `mq_open`, `mq_send`, `mq_receive`.
- Các thuộc tính queue.
- Cách tạo và xóa queue.
- Xử lý lỗi queue đầy hoặc message quá dài.

### Thành viên 2

Thuyết trình:

- Kiến trúc server.
- Luồng nhận và xử lý request.
- Định nghĩa protocol.
- Logic xử lý từng command.
- Cách điều hướng response về đúng client.

### Thành viên 3

Thuyết trình:

- Luồng hoạt động client.
- Cách tạo queue response theo PID.
- Cách chạy nhiều client đồng thời.
- Demo trực tiếp toàn bộ hệ thống.
- Cách tránh lẫn response giữa các client.

### Thành viên 4

Thuyết trình:

- Cách build project bằng Makefile.
- Cách chạy và kiểm tra kết quả.
- Các test đã thực hiện.
- Kết quả stress test.
- Hướng dẫn chạy lại từ máy khác.

## 12. Tiêu chí đánh giá công việc

Điểm của từng thành viên được đánh giá dựa trên:

1. Chất lượng phần code phụ trách.
2. Mức độ hoàn thành Definition of Done.
3. Tài liệu hoặc comment cần thiết.
4. Khả năng phối hợp với phần của thành viên khác.
5. Code review của Pull Request.
6. Hiểu và giải thích được phần của mình.
7. Không vi phạm protocol chung.
8. Không để queue hoặc process rác sau demo.

## 13. Định nghĩa dự án hoàn thành

Dự án được xem là hoàn thành khi:

- Server và client chạy trên Linux/WSL bằng ngôn ngữ C.
- Dữ liệu được trao đổi bằng POSIX Message Queue.
- Ít nhất một queue dùng chung và queue riêng theo client được sử dụng.
- Nhiều client có thể gửi request đồng thời.
- Response được gửi đúng client.
- Các command `PING`, `TIME`, `FIB`, `UPPER` hoạt động.
- Message lỗi được xử lý.
- Queue được xóa sạch khi kết thúc.
- Makefile và README chạy được trên máy mới.
- Có test tự động hoặc script test.
- Mỗi thành viên hiểu rõ phần của mình và phần liên quan của nhóm.
