# Cấu trúc và yêu cầu dự án

# Shared Message Queues trong Linux IPC bằng ngôn ngữ C

## 1. Mục tiêu

Xây dựng chương trình mô phỏng giao tiếp liên tiến trình bằng **POSIX Message Queue** trên Linux.

Hệ thống gồm:

- Một tiến trình **server** nhận request.
- Nhiều tiến trình **client** gửi request đồng thời.
- Queue `/shared_requests` dùng chung để nhận request.
- Queue `/shared_responses_<PID>` riêng cho từng client để nhận response.

Sau khi hoàn thành, nhóm phải giải thích được:

- POSIX Message Queue hoạt động ra sao.
- Client và server trao đổi dữ liệu như thế nào.
- Một queue nhận message từ nhiều client ở cấp độ IPC.
- Queue riêng giúp mỗi client nhận đúng response.
- Các lỗi thường gặp và cách đóng, hủy queue.

## 2. Công cụ thống nhất

| Hạng mục | Công cụ hoặc quy ước |
|---|---|
| Ngôn ngữ | C chuẩn POSIX |
| Biên dịch | GCC |
| Build | Make |
| Hệ điều hành | Linux hoặc WSL |
| IPC | POSIX Message Queue |
| Mã nguồn | Git và GitHub |
| Trình soạn thảo | VS Code |
| Kiểm thử | Test C và shell script |

Ví dụ liên kết thư viện:

```makefile
LDLIBS += -lrt
```

## 3. Kiến trúc hệ thống

```text
Client A ─┐
          ├──> /shared_requests ──> Server
Client B ─┤                           │
          │                           ├──> /shared_responses_<PID_A>
Client C ─┘                           └──> /shared_responses_<PID_B>
```

Luồng hoạt động:

1. Client tạo queue response riêng.
2. Client gửi request đến `/shared_requests`.
3. Server nhận request và phân tích nội dung.
4. Server xử lý request.
5. Server gửi response đến queue có PID tương ứng.
6. Client nhận response và hiển thị kết quả.
7. Client đóng và xóa queue response của mình.
8. Server dừng và xóa `/shared_requests`.

## 4. Quy ước queue

### Queue request dùng chung

```text
Tên queue: /shared_requests
Số message tối đa: 10
Kích thước tối đa của một message: 256 byte
Quyền tạo: 0660 hoặc 0600
```

### Queue response riêng của client

```text
Tên queue: /shared_responses_<PID>
Ví dụ PID 1234: /shared_responses_1234
Số message tối đa: 10
Kích thước tối đa của một message: 256 byte
```

### Các API bắt buộc

```c
mq_open();
mq_send();
mq_receive();
mq_getattr();
mq_close();
mq_unlink();
```

## 5. Protocol giao tiếp

### 5.1. Request

Định dạng:

```text
<PID>|<COMMAND>|<ARGUMENT>
```

Ví dụ:

```text
1001|PING|
1001|TIME|
1002|FIB|10
1003|UPPER|xin-chao
```

| Command | Argument | Response mong đợi |
|---|---|---|
| `PING` | Không có | `OK\|PONG` |
| `TIME` | Không có | `OK\|<thời gian server>` |
| `FIB` | Số nguyên không âm | `OK\|<giá trị Fibonacci>` |
| `UPPER` | Chuỗi không chứa dấu `\|` | `OK\|<chuỗi chữ hoa>` |
| Command không hợp lệ | Không áp dụng | `ERROR\|UNKNOWN_COMMAND` |

### 5.2. Response

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
ERROR|INVALID_REQUEST
```

### 5.3. Quy tắc chung

- Một message tối đa 256 byte.
- Luôn thêm ký tự kết thúc chuỗi sau `mq_receive()`.
- Không dùng dấu `|` trong argument.
- Không ghi chồng bộ nhớ khi nhận message.
- Request sai định dạng trả về lỗi thay vì làm server crash.
- Response phải gửi vào queue response có PID tương ứng.
- Không để queue tồn tại sau khi server hoặc client kết thúc.

## 6. Cấu trúc thư mục đề xuất

```text
linux-shared-message-queues/
├── Makefile
├── README.md
├── PHAN_CONG_NHIEM_VU.md
├── CAU_TRUC_YEU_CAU.md
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

Tên thư mục hoặc tên file có thể thay đổi khi cả nhóm thống nhất, nhưng không được gom toàn bộ mã nguồn vào một file duy nhất.

## 7. Yêu cầu chức năng

### 7.1. Phần queue

- Tạo và mở queue bằng POSIX API.
- Kiểm tra queue đã tồn tại hay chưa.
- Gửi message có priority.
- Nhận message và xử lý lỗi.
- Đóng và hủy queue.
- Hỗ trợ timeout hoặc chế độ non-blocking cho queue đầy/rỗng.

### 7.2. Phần server

- Tạo và mở `/shared_requests`.
- Nhận message liên tục.
- Xác thực request.
- Phân tích request theo dấu `|`.
- Thực hiện tối thiểu bốn command:
  - `PING`
  - `TIME`
  - `FIB`
  - `UPPER`
- Gửi response đến đúng queue response của client.
- In log request và response.
- Dừng an toàn bằng `Ctrl+C` và xóa queue request.

### 7.3. Phần client

- Nhận command từ dòng lệnh.
- Tạo queue `/shared_responses_<PID>`.
- Gửi request theo protocol.
- Chờ response bằng timeout hoặc blocking mode.
- In PID, queue name, request và response.
- Đóng và xóa queue response khi kết thúc.

Ví dụ:

```bash
./bin/client PING
./bin/client TIME
./bin/client FIB 10
./bin/client UPPER xin-chao
```

### 7.4. Phần build và demo

- Có Makefile build server và client.
- Có `make clean` xóa toàn bộ file build.
- Có script demo chạy nhiều client.
- Có README hướng dẫn chạy trên Linux hoặc WSL.
- Có log hoặc hình ảnh kết quả demo.

## 8. Các trường hợp lỗi bắt buộc

- Queue không tồn tại.
- Queue đã tồn tại.
- Queue đầy.
- Queue rỗng hoặc hết timeout.
- Message quá dài.
- Request sai định dạng.
- Command không hợp lệ.
- Argument không hợp lệ.
- Server chưa chạy.
- Một số client chạy đồng thời.

Lỗi phải được xử lý bằng thông báo rõ ràng, không crash và không để queue hoặc process rác.

## 9. Danh sách test bắt buộc

| Tên test | Kết quả mong đợi |
|---|---|
| Tạo và mở queue | Queue được tạo hoặc mở đúng |
| Gửi và nhận một message | Nội dung nhận đúng và đầy đủ |
| Xóa queue | `/shared_requests` không còn sau khi kết thúc |
| Ping/pong | Trả về `OK\|PONG` |
| Fibonacci 0, 10, 20 | Tính đúng các kết quả |
| Uppercase | Chuỗi được chuyển thành chữ hoa |
| Request sai | Trả về lỗi và server vẫn hoạt động |
| Hai client đồng thời | Response không bị lẫn giữa client |
| Queue đầy | Có timeout hoặc xử lý `EAGAIN` |
| Message quá dài | Không crash hoặc ghi đè bộ nhớ |
| Stress test | Nhiều message xử lý ổn định |
| Build sạch | `make clean && make` thành công |

## 10. Checklist hoàn thành dự án

- [ ] Server và client build bằng GCC/Make.
- [ ] POSIX Message Queue được sử dụng cho cả request và response.
- [ ] Nhiều client chạy đồng thời.
- [ ] Client nhận response từ queue của chính mình.
- [ ] Bốn command hoạt động.
- [ ] Request và argument sai được xử lý.
- [ ] Queue được tạo và dọn dẹp đúng.
- [ ] Không còn process demo chạy ngầm.
- [ ] Có test và script demo.
- [ ] Có README hướng dẫn chạy từ máy mới.
- [ ] Có kết quả test và log demo thực tế.
- [ ] Slide hoặc bài thuyết trình giải thích kiến trúc và IPC.

## 11. Chuẩn build và code

Build đề xuất:

```bash
make clean
make
```

Có thể thêm cờ cảnh báo:

```text
-Wall -Wextra -Werror -pedantic
```

Tiêu chí code:

- Không sử dụng tài nguyên toàn cục không cần thiết.
- Thất bại API phải kiểm tra `errno` và kết quả trả về.
- Không để tài nguyên queue không được đóng.
- Tên hàm và biến có ý nghĩa.
- Không thay đổi protocol riêng lẻ trên một phía.
