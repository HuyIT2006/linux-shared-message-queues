# Phân chia nhiệm vụ dự án

# Shared Message Queues trong Linux IPC bằng ngôn ngữ C


## Nhóm làm việc thống nhất

### Quy trình Git

Nhánh chính:

```text
main
```

Nhánh feature đề xuất:

```text
feature/tv1-mqueue-core
feature/tv2-server-protocol
feature/tv3-client-demo
feature/tv4-build-test-docs
```

Quy tắc làm việc:

1. Không commit trực tiếp vào `main`.
2. Cập nhật `main` trước khi tạo nhánh mới.
3. Tự kiểm thử phần mình phụ trách.
4. Tạo Pull Request.
5. Ít nhất một thành viên khác review.
6. Merge chỉ khi build thành công.
7. Khi sửa protocol phải cập nhật server, client và tài liệu cùng lúc.




Không commit thư mục build, binary, queue test hoặc dữ liệu cá nhân.

## Thành viên 1 — Queue Core

### Mục tiêu

Xây dựng module nền tảng thao tác POSIX Message Queue và xử lý lỗi IPC.

### Việc cần làm

- Viết `include/mqueue.h`.
- Viết `src/mqueue.c`.
- Đóng gói `mq_open()` thành hàm mở hoặc tạo queue.
- Viết hàm gửi và nhận message.
- Viết hàm đóng và hủy queue.
- Kiểm tra thuộc tính queue bằng `mq_getattr()`.
- Xử lý các lỗi:
  - Queue không tồn tại.
  - Queue đã tồn tại.
  - Queue đầy.
  - Message quá dài.
  - Không có quyền truy cập.
  - Tham số không hợp lệ.

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

### Sản phẩm

- Module queue hoạt động.
- Test gửi và nhận một message.
- Test tạo queue, mở lại queue và xóa queue.
- Không để queue tồn tại sau khi kết thúc.

### Hoàn thành khi

- Server và client có thể dùng chung module.
- Queue đầy hoặc message quá dài không làm chương trình crash.
- Lỗi API được kiểm tra và có thông báo rõ ràng.
- Tên queue và thuộc tính được định nghĩa nhất quán.

## Thành viên 2 — Server và xử lý protocol

### Mục tiêu

Viết server nhận request, thực hiện command và gửi response về queue riêng của client.

### Việc cần làm

- Viết `src/server.c`.
- Viết hoặc cập nhật `include/protocol.h`.
- Mở hoặc tạo `/shared_requests`.
- Nhận message trong vòng lặp liên tục.
- Phân tích request theo format:

```text
<PID>|<COMMAND>|<ARGUMENT>
```

- Thực hiện command `PING`, `TIME`, `FIB`, `UPPER`.
- Kiểm tra số nguyên Fibonacci hợp lệ.
- Chuyển chuỗi sang chữ hoa.
- Tính queue response từ PID.
- Gửi response theo format `STATUS|MESSAGE`.
- In log request và response.
- Đăng ký xử lý `SIGINT/SIGTERM` nếu cần.
- Dọn dẹp queue khi server kết thúc.

### Hàm đề xuất

```c
int parse_request(const char *input, Request *request);
int execute_command(const Request *request, char *response,
                    size_t response_size);
int send_response_to_client(pid_t client_pid,
                            const char *response);
```

### Sản phẩm

- Server nhận request liên tục.
- Đủ các command đã thống nhất.
- Request sai không làm server dừng.
- Hai client nhận response riêng biệt.
- Ctrl+C dọn dẹp queue sạch sẽ.

### Hoàn thành khi

- `PING` trả về `OK|PONG`.
- `FIB 10` trả về `55`.
- `UPPER xin-chao` trả về `XIN-CHAO`.
- Command sai trả về lỗi protocol.
- Không có message response gửi nhầm client.

## Thành viên 3 — Client và kịch bản demo

### Mục tiêu

Viết client dễ sử dụng và script demo minh họa nhiều tiến trình cùng hoạt động.

### Việc cần làm

- Viết `src/client.c`.
- Nhận command từ dòng lệnh.
- Lấy PID của client.
- Tạo `/shared_responses_<PID>`.
- Kiểm tra queue request tồn tại.
- Gửi request theo protocol chung.
- Nhận và hiển thị response.
- In PID, tên queue, request và response.
- Đóng và xóa queue response khi kết thúc.
- Viết `demo/demo.sh`.
- Demo chạy nhiều client bằng background process.
- Viết demo `PING`, `TIME`, `FIB`, `UPPER` và stress test.

### Lệnh demo client đề xuất

```bash
./bin/client PING
./bin/client TIME
./bin/client FIB 10
./bin/client UPPER xin-chao
```

### Lệnh demo nhiều client

```bash
./bin/client PING &
./bin/client FIB 10 &
./bin/client UPPER linux-ipc &
wait
```

### Sản phẩm

- Client chạy bằng dòng lệnh.
- Script demo chạy được trên Linux/WSL.
- Response không bị lẫn giữa các client.
- Không để lại queue response sau demo.
- Log demo dễ hiểu cho người xem.

### Hoàn thành khi

- Một người xem có thể chạy server, rồi chạy script demo.
- Mỗi client nhận response tại đúng queue của mình.
- Script không để process ngầm sau khi kết thúc.
- Timeout được xử lý khi server không phản hồi.

## Thành viên 4 — Build, kiểm thử và tài liệu

### Mục tiêu

Đảm bảo project build sạch, có test, có tài liệu chạy được và có slide/demo hợp lý.

### Việc cần làm

#### Makefile và build

- Viết `Makefile`.
- Tạo thư mục `bin/` và `obj/`.
- Tạo các target:
  - `all`
  - `server`
  - `client`
  - `clean`
  - `test`
  - `run-server`
  - `run-demo`
- Liên kết thư viện POSIX phù hợp.
- Thêm thư mục `include/` vào GCC include path.
- Có thể thêm cờ `-Wall -Wextra -Werror -pedantic`.

#### Kiểm thử

Viết hoặc tích hợp:

- Test tạo/mở/xóa queue.
- Test gửi và nhận message.
- Test queue response của hai client.
- Test `PING`, `TIME`, `FIB`, `UPPER`.
- Test command và argument sai.
- Test queue đầy và message quá dài.
- Test nhiều client đồng thời.
- Stress test ít nhất 100 message hoặc theo thống nhất nhóm.

Ví dụ:

```bash
make clean && make
make test
./demo/demo.sh
```

#### Tài liệu

- Viết hoặc cập nhật `README.md`.
- Viết hướng dẫn cài đặt GCC và chạy trên Linux/WSL.
- Chụp log hoặc kết quả demo thực tế.
- Viết slide thuyết trình.
- Liệt kê lỗi thường gặp và cách xử lý.
- Kiểm tra hướng dẫn có thể chạy lại từ máy mới.

#### Sản phẩm

- Project build từ thư mục sạch.
- Script test chạy lại được.
- README hoàn chỉnh.
- Slide demo có kiến trúc và kết quả.
- Danh sách các queue còn tồn tại sau khi test.

### Hoàn thành khi

- `make clean && make` thành công.
- Không có warning nghiêm trọng.
- `make test` hoặc script test có kết quả rõ ràng.
- Một người khác có thể chạy lại demo theo README.
- Không còn queue test sau demo.

## 7. Thứ tự thực hiện

### Giai đoạn 1 — Khởi tạo

Người phụ trách chính: Thành viên 4 phối hợp cả nhóm.

- Tạo thư mục và Makefile.
- Tạo header protocol chung.
- Thống nhất tên queue và kích thước message.
- Chia nhánh Git.

Kết quả: Project build được và có interface chung.

### Giai đoạn 2 — Queue core và protocol

Người phụ trách chính: Thành viên 1 và 2.

- Viết queue wrapper.
- Viết parser request.
- Viết response cơ bản.

Kết quả: Một message được gửi và nhận thành công.

### Giai đoạn 3 — Ghép server và client

Người phụ trách chính: Thành viên 2 và 3.

- Tạo queue response theo PID.
- Ghép server với queue wrapper.
- Ghép client với queue wrapper.
- Chạy thử `PING`.

Kết quả: Client nhận đúng response của mình.

### Giai đoạn 4 — Demo và kiểm thử

Người phụ trách chính: Thành viên 3 và 4.

- Viết demo nhiều client.
- Viết test.
- Chạy stress test.
- Viết README và slide.

Kết quả: Demo ổn định, không queue rác.

## 8. Danh sách phụ thuộc công việc

| Công việc | Cần hoàn thành trước |
|---|---|
| Viết server | Queue wrapper và protocol |
| Viết client | Queue wrapper và protocol |
| Ghép server/client | Header chung và tên queue thống nhất |
| Integration test | Server và client hoạt động |
| Demo nhiều client | Hai client nhận đúng response riêng |
| Slide demo | Kết quả build, test và demo thực tế |

## 9. Nhiệm vụ thuyết trình

### Thành viên 1

- POSIX Message Queue là gì.
- API tạo, gửi, nhận, đóng và xóa queue.
- Cách xử lý queue đầy và message lỗi.

### Thành viên 2

- Kiến trúc server.
- Luồng nhận và xử lý request.
- Command và xử lý response.
- Cách gửi response đúng client.

### Thành viên 3

- Luồng hoạt động client.
- Queue response theo PID.
- Demo nhiều client.
- Cách tránh lẫn response.

### Thành viên 4

- Makefile và build.
- Test và stress test.
- Hướng dẫn chạy project.
- Kết quả demo và tài liệu.

## 10. Phân công test

| Test | Người phụ trách | Người hỗ trợ |
|---|---|---|
| Queue tạo/mở/xóa | Thành viên 1 | Thành viên 4 |
| Gửi/nhận message | Thành viên 1 | Thành viên 2 |
| `PING`, `TIME`, `FIB`, `UPPER` | Thành viên 2 | Thành viên 4 |
| Client nhận đúng queue | Thành viên 3 | Thành viên 2 |
| Hai client đồng thời | Thành viên 3 | Thành viên 2, 4 |
| Queue đầy/message dài | Thành viên 1 | Thành viên 4 |
| Build/stress test | Thành viên 4 | Thành viên 1, 2, 3 |

## 11. Checklist bàn giao cá nhân

### Thành viên 1

- [ ] Queue wrapper hoàn thiện.
- [ ] Xử lý lỗi IPC đầy đủ.
- [ ] Test queue hoạt động.

### Thành viên 2

- [ ] Server nhận request liên tục.
- [ ] Bốn command hoạt động.
- [ ] Response đúng queue client.

### Thành viên 3

- [ ] Client nhận tham số dòng lệnh.
- [ ] Queue response theo PID hoạt động.
- [ ] Script demo nhiều client hoạt động.

### Thành viên 4

- [ ] Makefile build server/client.
- [ ] `make test` hoạt động.
- [ ] README và slide hoàn chỉnh.

## 12. Tiêu chí đánh giá

- Hoàn thành đúng phần phụ trách.
- Hiểu và giải thích được mã của mình.
- Không vi phạm protocol chung.
- Phối hợp khi ghép từng module.
- Tạo Pull Request có review.
- Viết test hoặc tài liệu liên quan phần phụ trách.
- Không để queue hoặc process rác.
