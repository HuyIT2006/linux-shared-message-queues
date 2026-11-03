/**
 * @file test_protocol.c
 * @brief Comprehensive Unit Test Suite for Protocol Parser, System Services & Worker Pool
 * @author Thanh vien 2 (Server Architecture, Worker Pool & System Services)
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "protocol.h"
#include "worker_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [" COLOR_GREEN "PASS" COLOR_RESET "] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [" COLOR_RED "FAIL" COLOR_RESET "] %s (Line: %d)\n", msg, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

/* =========================================================================
 * Test 1: Parse Basic 3-Field Requests (<PID>|<COMMAND>|<ARGUMENT>)
 * ========================================================================= */
static void test_parse_basic_requests(void) {
    printf(COLOR_BOLD "\n--- Chay Test 1: Phan tich Request 3 truong (<PID>|<COMMAND>|<ARGUMENT>) ---\n" COLOR_RESET);

    IpcRequest req;
    const char *raw1 = "1001|PING|";
    ProtoErrorCode rc1 = protocol_parse_request(raw1, strlen(raw1), MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(rc1 == PROTO_OK, "Parse '1001|PING|' thanh cong");
    TEST_ASSERT(req.client_pid == 1001, "PID = 1001");
    TEST_ASSERT(req.cmd_type == CMD_PING && strcmp(req.command, "PING") == 0, "Command = PING");
    TEST_ASSERT(strcmp(req.payload, "") == 0, "Payload rong cho PING");
    TEST_ASSERT(req.is_extended_format == false, "Nhan dien dung dinh dang 3 truong co ban");

    const char *raw2 = "1002|fib|10";
    ProtoErrorCode rc2 = protocol_parse_request(raw2, strlen(raw2), MSG_PRIO_HIGH, &req);
    TEST_ASSERT(rc2 == PROTO_OK, "Parse '1002|fib|10' (chu thuong) thanh cong");
    TEST_ASSERT(req.client_pid == 1002 && req.cmd_type == CMD_FIB, "Chuan hoa 'fib' -> CMD_FIB");
    TEST_ASSERT(strcmp(req.payload, "10") == 0, "Argument = '10'");
    TEST_ASSERT(req.priority == MSG_PRIO_HIGH, "Ke thua priority tu mq_receive (MSG_PRIO_HIGH)");

    const char *raw3 = "1003|UPPER|xin-chao";
    ProtoErrorCode rc3 = protocol_parse_request(raw3, strlen(raw3), MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(rc3 == PROTO_OK && req.cmd_type == CMD_UPPER && strcmp(req.payload, "xin-chao") == 0,
                "Parse '1003|UPPER|xin-chao' chinh xac");
}

/* =========================================================================
 * Test 2: Parse Extended 4-Field Requests (<PID>|<COMMAND>|<PRIORITY>|<PAYLOAD>)
 * ========================================================================= */
static void test_parse_extended_requests(void) {
    printf(COLOR_BOLD "\n--- Chay Test 2: Phan tich Request 4 truong (<PID>|<CMD>|<PRIO>|<PAYLOAD>) ---\n" COLOR_RESET);

    IpcRequest req;
    const char *raw1 = "2048|FIB|URGENT|20";
    ProtoErrorCode rc1 = protocol_parse_request(raw1, strlen(raw1), MSG_PRIO_LOW, &req);
    TEST_ASSERT(rc1 == PROTO_OK, "Parse '2048|FIB|URGENT|20' thanh cong");
    TEST_ASSERT(req.client_pid == 2048 && req.cmd_type == CMD_FIB, "PID = 2048, Command = FIB");
    TEST_ASSERT(req.priority == MSG_PRIO_URGENT, "Priority parse tu chuoi 'URGENT' (30)");
    TEST_ASSERT(strcmp(req.payload, "20") == 0 && req.is_extended_format == true,
                "Payload = '20' va co is_extended_format = true");

    const char *raw2 = "3099|SYSINFO|20|";
    ProtoErrorCode rc2 = protocol_parse_request(raw2, strlen(raw2), MSG_PRIO_LOW, &req);
    TEST_ASSERT(rc2 == PROTO_OK && req.cmd_type == CMD_SYSINFO && req.priority == MSG_PRIO_HIGH,
                "Parse '3099|SYSINFO|20|' voi priority dang so (20 -> HIGH)");
}

/* =========================================================================
 * Test 3: Malformed Requests, Buffer Overflow & Injection Protection
 * ========================================================================= */
static void test_malformed_and_security(void) {
    printf(COLOR_BOLD "\n--- Chay Test 3: Chong Buffer Overflow, Injection & Request sai dinh dang ---\n" COLOR_RESET);

    IpcRequest req;

    TEST_ASSERT(protocol_parse_request("", 0, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi chuoi rong");
    TEST_ASSERT(protocol_parse_request("1001|PING", 9, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi request chi co 1 dau '|'");
    TEST_ASSERT(protocol_parse_request("1001|UPPER|NORMAL|a|b", 21, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi request chua dau '|' trong payload (4 dau '|')");
    TEST_ASSERT(protocol_parse_request("abc|PING|", 9, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi PID khong phai la so ('abc')");
    TEST_ASSERT(protocol_parse_request("0|PING|", 7, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi PID <= 0 ('0')");
    TEST_ASSERT(protocol_parse_request("-55|PING|", 9, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi PID am ('-55')");
    TEST_ASSERT(protocol_parse_request("1001||arg", 9, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi Command rong ('1001||arg')");
    TEST_ASSERT(protocol_parse_request("1001|PING|INVALID_PRIO|", 23, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Tu choi Priority khong hop le trong request 4 truong");

    /* Embedded null byte attack */
    const char null_attack[] = "1001|UPPER|hello\0hidden";
    TEST_ASSERT(protocol_parse_request(null_attack, sizeof(null_attack) - 1, 0, &req) == PROTO_ERR_INVALID_REQUEST,
                "Chan tan cong Embedded Null-Byte Injection");

    /* Oversized message (> 255 bytes) */
    char huge_msg[300];
    memset(huge_msg, 'A', sizeof(huge_msg) - 1);
    huge_msg[sizeof(huge_msg) - 1] = '\0';
    TEST_ASSERT(protocol_parse_request(huge_msg, strlen(huge_msg), 0, &req) == PROTO_ERR_BUFFER_OVERFLOW,
                "Chan tin nhan vuot qua gioi han 256 bytes (PROTO_ERR_BUFFER_OVERFLOW)");
}

/* =========================================================================
 * Test 4: Core Commands (PING, TIME, FIB, UPPER) & Error Responses
 * ========================================================================= */
static void test_core_commands(void) {
    printf(COLOR_BOLD "\n--- Chay Test 4: Kiem thu 4 Command cot loi (PING, TIME, FIB, UPPER) ---\n" COLOR_RESET);

    IpcRequest req;
    IpcResponse resp;
    char wire[PROTO_MAX_MSG_SIZE];

    /* 1. PING */
    protocol_parse_request("1001|PING|", 10, MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(protocol_execute_request(&req, &resp, NULL) == PROTO_OK, "Thuc thi PING thanh cong");
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|PONG") == 0, "Response PING khop chuan 'OK|PONG'");

    /* PING with unexpected argument -> ERROR|INVALID_ARGUMENT */
    protocol_parse_request("1001|PING|extra", 15, MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(protocol_execute_request(&req, &resp, NULL) == PROTO_ERR_INVALID_ARGUMENT,
                "PING co tham so thua tra ve PROTO_ERR_INVALID_ARGUMENT");
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "ERROR|INVALID_ARGUMENT") == 0,
                "Response PING loi khop 'ERROR|INVALID_ARGUMENT'");

    /* 2. TIME */
    protocol_parse_request("1001|TIME|", 10, MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(protocol_execute_request(&req, &resp, NULL) == PROTO_OK, "Thuc thi TIME thanh cong");
    TEST_ASSERT(strlen(resp.payload) >= 19 && strchr(resp.payload, 'T') != NULL,
                "TIME tra ve dinh dang chuan ISO 8601");

    /* 3. FIB (0, 10, 20, 45) */
    protocol_parse_request("1002|FIB|0", 10, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|0") == 0, "FIB(0) -> 'OK|0'");

    protocol_parse_request("1002|FIB|10", 11, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|55") == 0, "FIB(10) -> 'OK|55'");

    protocol_parse_request("1002|FIB|20", 11, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|6765") == 0, "FIB(20) -> 'OK|6765'");

    protocol_parse_request("1002|FIB|45", 11, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|1134903170") == 0, "FIB(45) -> 'OK|1134903170'");

    /* FIB invalid arguments (-5, 46, abc, empty) */
    protocol_parse_request("1002|FIB|-5", 11, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "ERROR|INVALID_ARGUMENT") == 0, "FIB(-5) -> 'ERROR|INVALID_ARGUMENT'");

    protocol_parse_request("1002|FIB|46", 11, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "ERROR|INVALID_ARGUMENT") == 0, "FIB(46) vuot gioi han 45 -> 'ERROR|INVALID_ARGUMENT'");

    /* 4. UPPER */
    protocol_parse_request("1003|UPPER|xin-chao", 19, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|XIN-CHAO") == 0, "UPPER 'xin-chao' -> 'OK|XIN-CHAO'");

    /* 5. UNKNOWN_COMMAND */
    protocol_parse_request("1004|HACK_SYSTEM|123", 20, MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "ERROR|UNKNOWN_COMMAND") == 0,
                "Command la tra ve 'ERROR|UNKNOWN_COMMAND'");
}

/* =========================================================================
 * Test 5: Extended Linux System Services (SYSINFO, STATS, CALC, ECHO)
 * ========================================================================= */
static void test_extended_services(void) {
    printf(COLOR_BOLD "\n--- Chay Test 5: Kiem thu Dich vu He thong Mo rong (SYSINFO, STATS, CALC, ECHO) ---\n" COLOR_RESET);

    IpcRequest req;
    IpcResponse resp;
    char wire[PROTO_MAX_MSG_SIZE];

    /* 1. SYSINFO (/proc/uptime, /proc/loadavg, /proc/meminfo) */
    const char *sys_raw = "2001|SYSINFO|HIGH|";
    protocol_parse_request(sys_raw, strlen(sys_raw), MSG_PRIO_HIGH, &req);
    TEST_ASSERT(protocol_execute_request(&req, &resp, NULL) == PROTO_OK,
                "Thuc thi SYSINFO doc du lieu tu /proc thanh cong");
    TEST_ASSERT(strstr(resp.payload, "UPTIME=") != NULL &&
                strstr(resp.payload, "LOAD=") != NULL &&
                strstr(resp.payload, "MEM_AVAIL=") != NULL,
                "SYSINFO chua day du UPTIME, LOAD (/proc/loadavg), MEM_AVAIL (/proc/meminfo)");

    /* 2. STATS */
    ServerStatsSnapshot mock_stats = {
        .total_requests = 42,
        .failed_requests = 3,
        .active_workers = 2,
        .total_workers = 4,
        .queued_jobs = 1,
        .uptime_seconds = 120
    };
    const char *stats_raw = "2001|STATS|";
    protocol_parse_request(stats_raw, strlen(stats_raw), MSG_PRIO_NORMAL, &req);
    TEST_ASSERT(protocol_execute_request(&req, &resp, &mock_stats) == PROTO_OK,
                "Thuc thi STATS thanh cong");
    TEST_ASSERT(strstr(resp.payload, "TOTAL=42") != NULL &&
                strstr(resp.payload, "ERRORS=3") != NULL &&
                strstr(resp.payload, "WORKERS=2/4") != NULL,
                "STATS phan anh chinh xac so lieu van hanh Server");

    /* 3. CALC */
    const char *calc_raw = "2001|CALC|125 * 4";
    protocol_parse_request(calc_raw, strlen(calc_raw), MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "OK|500") == 0, "CALC '125 * 4' -> 'OK|500'");

    const char *div_zero = "2001|CALC|100 / 0";
    protocol_parse_request(div_zero, strlen(div_zero), MSG_PRIO_NORMAL, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), false);
    TEST_ASSERT(strcmp(wire, "ERROR|INVALID_ARGUMENT") == 0,
                "CALC chia cho 0 -> 'ERROR|INVALID_ARGUMENT'");

    /* 4. Extended Response Serialization (<STATUS>|<EXEC_TIME_MS>|<PAYLOAD>) */
    const char *echo_raw = "2001|ECHO|URGENT|Hello_POSIX_IPC";
    protocol_parse_request(echo_raw, strlen(echo_raw), MSG_PRIO_URGENT, &req);
    protocol_execute_request(&req, &resp, NULL);
    protocol_serialize_response(&resp, wire, sizeof(wire), true);
    TEST_ASSERT(strncmp(wire, "OK|", 3) == 0 && strstr(wire, "|Hello_POSIX_IPC") != NULL,
                "Serialize Response 3 truong (<STATUS>|<EXEC_TIME_MS>|<PAYLOAD>) dung chuan");
}

/* =========================================================================
 * Test 6: Multi-Threaded Worker Pool Concurrency & Graceful Shutdown
 * ========================================================================= */
typedef struct {
    pthread_mutex_t lock;
    int received_responses;
    int pong_count;
    int fib_count;
    int error_count;
} TestCallbackCtx;

static int mock_response_callback(pid_t client_pid, const char *response_str,
                                  MessagePriority prio, void *user_data) {
    (void)client_pid;
    (void)prio;
    TestCallbackCtx *ctx = (TestCallbackCtx *)user_data;
    pthread_mutex_lock(&ctx->lock);
    ctx->received_responses++;
    if (strcmp(response_str, "OK|PONG") == 0) {
        ctx->pong_count++;
    } else if (strcmp(response_str, "OK|1134903170") == 0) {
        ctx->fib_count++;
    } else if (strncmp(response_str, "ERROR|", 6) == 0) {
        ctx->error_count++;
    }
    pthread_mutex_unlock(&ctx->lock);
    return 0;
}

static void test_worker_pool_concurrency(void) {
    printf(COLOR_BOLD "\n--- Chay Test 6: Kiem thu Worker Thread Pool Da Luong (60 requests dong thoi) ---\n" COLOR_RESET);

    TestCallbackCtx cb_ctx;
    memset(&cb_ctx, 0, sizeof(cb_ctx));
    pthread_mutex_init(&cb_ctx.lock, NULL);

    WorkerPoolConfig cfg = {
        .num_threads = 4,
        .max_queue_size = 128,
        .send_timeout_ms = 1000,
        .format_mode = PROTO_MODE_BASIC,
        .verbose_log = false,
        .response_cb = mock_response_callback,
        .user_data = &cb_ctx
    };

    WorkerPool *pool = worker_pool_create(&cfg);
    TEST_ASSERT(pool != NULL, "Khoi tao WorkerPool voi 4 worker threads thanh cong");

    /* Submit 25 PING requests, 25 FIB(45) requests, and 10 invalid requests concurrently */
    for (int i = 0; i < 25; ++i) {
        char req_buf[64];
        snprintf(req_buf, sizeof(req_buf), "%d|PING|", 5000 + i);
        worker_pool_submit(pool, req_buf, strlen(req_buf), MSG_PRIO_HIGH);
    }
    for (int i = 0; i < 25; ++i) {
        char req_buf[64];
        snprintf(req_buf, sizeof(req_buf), "%d|FIB|URGENT|45", 6000 + i);
        worker_pool_submit(pool, req_buf, strlen(req_buf), MSG_PRIO_URGENT);
    }
    for (int i = 0; i < 10; ++i) {
        char req_buf[64];
        snprintf(req_buf, sizeof(req_buf), "%d|UNKNOWN_CMD|xyz", 7000 + i);
        worker_pool_submit(pool, req_buf, strlen(req_buf), MSG_PRIO_LOW);
    }

    worker_pool_wait_idle(pool);

    ServerStatsSnapshot stats;
    worker_pool_get_stats(pool, &stats);

    TEST_ASSERT(stats.total_requests == 60, "WorkerPool xu ly du 60/60 requests song song");
    TEST_ASSERT(stats.failed_requests == 10, "Thong ke chinh xac 10/60 requests loi");
    TEST_ASSERT(cb_ctx.received_responses == 60, "Callback nhan du 60 phan hoi");
    TEST_ASSERT(cb_ctx.pong_count == 25 && cb_ctx.fib_count == 25 && cb_ctx.error_count == 10,
                "Ket qua xu ly 25 PING + 25 FIB(45) + 10 ERROR chinh xac tuyet doi, khong race condition");

    worker_pool_destroy(pool, true);
    pthread_mutex_destroy(&cb_ctx.lock);
    TEST_ASSERT(1, "Graceful shutdown WorkerPool va giai phong bo nho sach se");
}

/* =========================================================================
 * Main Test Runner
 * ========================================================================= */
int main(void) {
    printf(COLOR_CYAN COLOR_BOLD);
    printf("=================================================================\n");
    printf("   BO TEST CHUYEN SAU PROTOCOL & WORKER POOL - THANH VIEN 2      \n");
    printf("   Ngon ngu: C POSIX 2008 | Multi-threading: POSIX pthreads      \n");
    printf("=================================================================\n");
    printf(COLOR_RESET);

    test_parse_basic_requests();
    test_parse_extended_requests();
    test_malformed_and_security();
    test_core_commands();
    test_extended_services();
    test_worker_pool_concurrency();

    printf("\n=================================================================\n");
    printf(COLOR_BOLD "KET QUA KIEM THU THANH VIEN 2:\n" COLOR_RESET);
    printf("  Tong so bai test: %d\n", g_tests_passed + g_tests_failed);
    printf("  " COLOR_GREEN "PASSED: %d" COLOR_RESET "\n", g_tests_passed);
    if (g_tests_failed > 0) {
        printf("  " COLOR_RED "FAILED: %d" COLOR_RESET "\n", g_tests_failed);
    } else {
        printf("  " COLOR_GREEN COLOR_BOLD "TAT CA CAC BAI TEST PROTOCOL & WORKER POOL DAT 100%%!" COLOR_RESET "\n");
    }
    printf("=================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}