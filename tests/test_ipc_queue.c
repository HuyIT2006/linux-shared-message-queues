/**
 * @file test_ipc_queue.c
 * @brief Comprehensive Unit Test Suite for IPC Core Engine
 * @author VanHuy (Thanh vien 1 - IPC Core Engine)
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "ipc_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

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
        printf("  [" COLOR_RED "FAIL" COLOR_RESET "] %s (Line: %d, errno: %d [%s])\n", \
               msg, __LINE__, errno, strerror(errno)); \
        g_tests_failed++; \
    } \
} while(0)

#define TEST_QUEUE_NAME "/test_ipc_core_vh"

static void cleanup_test_queue(void) {
    ipc_queue_unlink(TEST_QUEUE_NAME);
}

/* =========================================================================
 * Test 1: Validate Queue Name
 * ========================================================================= */
static void test_validate_queue_name(void) {
    printf(COLOR_BOLD "\n--- Chay Test 1: Kiem tra quy tac dat ten queue ---\n" COLOR_RESET);

    TEST_ASSERT(ipc_queue_validate_name("/valid_queue") == 0,
                "Ten hop le bat dau bang '/': /valid_queue");
    TEST_ASSERT(ipc_queue_validate_name("/shared_requests") == 0,
                "Ten hop le: /shared_requests");

    TEST_ASSERT(ipc_queue_validate_name("invalid_no_slash") != 0 && errno == EINVAL,
                "Tu choi ten khong bat dau bang '/': invalid_no_slash");
    TEST_ASSERT(ipc_queue_validate_name("/invalid/multi/slash") != 0 && errno == EINVAL,
                "Tu choi ten chua nhieu dau gach cheo: /invalid/multi/slash");
    TEST_ASSERT(ipc_queue_validate_name(NULL) != 0 && errno == EINVAL,
                "Tu choi ten la con tro NULL");
    TEST_ASSERT(ipc_queue_validate_name("/") != 0 && errno == EINVAL,
                "Tu choi ten chi chua moi dau gach cheo '/'");

    char response_name[64];
    TEST_ASSERT(ipc_queue_build_response_name(response_name, sizeof(response_name), 9876) == 0,
                "Tao ten queue response tu PID thanh cong");
    TEST_ASSERT(strcmp(response_name, "/shared_responses_9876") == 0,
                "Dinh dang ten response dung chuan: /shared_responses_9876");
}

/* =========================================================================
 * Test 2: Create, Open, Close, and Unlink Queue
 * ========================================================================= */
static void test_create_open_close_unlink(void) {
    printf(COLOR_BOLD "\n--- Chay Test 2: Vong doi Queue (Create -> Open -> Close -> Unlink) ---\n" COLOR_RESET);
    cleanup_test_queue();

    QueueConfig cfg = {
        .max_messages = 5,
        .max_msg_size = 128,
        .is_nonblocking = 0
    };

    /* 1. Tao moi voi O_CREAT | O_EXCL */
    mqd_t q1 = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT | O_EXCL, 0660, &cfg);
    TEST_ASSERT(q1 != (mqd_t)-1, "Tao moi hang doi voi O_CREAT | O_EXCL");

    /* 2. Kiem tra thuoc tinh queue */
    struct mq_attr attr;
    TEST_ASSERT(ipc_queue_get_attr(q1, &attr) == 0, "Doc thuoc tinh queue thanh cong");
    TEST_ASSERT(attr.mq_maxmsg == 5, "Thuoc tinh max_messages duoc thiet lap dung (= 5)");
    TEST_ASSERT(attr.mq_msgsize == 128, "Thuoc tinh max_msg_size duoc thiet lap dung (= 128)");
    TEST_ASSERT(ipc_queue_get_curmsgs(q1) == 0, "So tin nhan ban dau la 0");

    /* 3. Tao lai queue da ton tai voi O_CREAT | O_EXCL -> phai bao loi EEXIST */
    mqd_t q_duplicate = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT | O_EXCL, 0660, &cfg);
    TEST_ASSERT(q_duplicate == (mqd_t)-1 && errno == EEXIST,
                "Bao loi EEXIST khi co tinh tao trung ten queue da ton tai");

    /* 4. Mo lai queue da ton tai (khong dung O_CREAT) */
    mqd_t q2 = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR, 0, NULL);
    TEST_ASSERT(q2 != (mqd_t)-1, "Mo lai queue da ton tai thanh cong");

    /* 5. Dong queue */
    TEST_ASSERT(ipc_queue_close(q1) == 0, "Dong descriptor q1");
    TEST_ASSERT(ipc_queue_close(q2) == 0, "Dong descriptor q2");

    /* 6. Xoa queue (unlink) */
    TEST_ASSERT(ipc_queue_unlink(TEST_QUEUE_NAME) == 0, "Xoa queue khoi he thong thanh cong");

    /* 7. Mo lai sau khi da xoa -> phai bao loi ENOENT */
    mqd_t q_deleted = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR, 0, NULL);
    TEST_ASSERT(q_deleted == (mqd_t)-1 && errno == ENOENT,
                "Mo queue da bi unlink bao loi ENOENT (Queue not found)");
}

/* =========================================================================
 * Test 3: Basic Message Send and Receive
 * ========================================================================= */
static void test_send_receive_basic(void) {
    printf(COLOR_BOLD "\n--- Chay Test 3: Gui va Nhan thong diep co ban ---\n" COLOR_RESET);
    cleanup_test_queue();

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, NULL);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue cho gui/nhan");

    const char *test_msg = "HELLO_LINUX_IPC_VANHUY";
    size_t test_len = strlen(test_msg);

    int send_rc = ipc_queue_send(q, test_msg, test_len, MSG_PRIO_NORMAL);
    TEST_ASSERT(send_rc == 0, "Gui thong diep thanh cong");
    TEST_ASSERT(ipc_queue_get_curmsgs(q) == 1, "So tin nhan trong queue tang len 1");

    char recv_buf[IPC_DEFAULT_MSG_SIZE + 1];
    unsigned int prio = 0;
    ssize_t bytes_recv = ipc_queue_receive(q, recv_buf, sizeof(recv_buf), &prio);

    TEST_ASSERT(bytes_recv == (ssize_t)test_len, "So byte nhan duoc khop voi so byte gui");
    TEST_ASSERT(strcmp(recv_buf, test_msg) == 0, "Noi dung thong diep nhan duoc hoan toan chinh xac");
    TEST_ASSERT(prio == MSG_PRIO_NORMAL, "Do uu tien (priority) nhan duoc khop voi MSG_PRIO_NORMAL (10)");
    TEST_ASSERT(ipc_queue_get_curmsgs(q) == 0, "Queue rong sau khi da doc xong");

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Test 4: Message Priority Ordering (CRITICAL TEST)
 * ========================================================================= */
static void test_message_priority(void) {
    printf(COLOR_BOLD "\n--- Chay Test 4: Kiem tra co che sap xep Do Uu Tien (Message Priority) ---\n" COLOR_RESET);
    cleanup_test_queue();

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, NULL);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue kiem tra priority");

    /* Gui theo thu tu: LOW (0) -> URGENT (30) -> NORMAL (10) */
    ipc_queue_send(q, "MSG_THAP", strlen("MSG_THAP"), MSG_PRIO_LOW);
    ipc_queue_send(q, "MSG_KHAN_CAP", strlen("MSG_KHAN_CAP"), MSG_PRIO_URGENT);
    ipc_queue_send(q, "MSG_CHUAN", strlen("MSG_CHUAN"), MSG_PRIO_NORMAL);

    TEST_ASSERT(ipc_queue_get_curmsgs(q) == 3, "Da dua 3 thong diep vao hang doi");

    char buf[IPC_DEFAULT_MSG_SIZE + 1];
    unsigned int prio = 0;

    /* Nhan tin 1: Bat buoc phai la URGENT (30) */
    ipc_queue_receive(q, buf, sizeof(buf), &prio);
    TEST_ASSERT(strcmp(buf, "MSG_KHAN_CAP") == 0 && prio == MSG_PRIO_URGENT,
                "Tin nhan dau tien nhan duoc la MSG_KHAN_CAP (Prio 30)");

    /* Nhan tin 2: Bat buoc phai la NORMAL (10) */
    ipc_queue_receive(q, buf, sizeof(buf), &prio);
    TEST_ASSERT(strcmp(buf, "MSG_CHUAN") == 0 && prio == MSG_PRIO_NORMAL,
                "Tin nhan thu hai nhan duoc la MSG_CHUAN (Prio 10)");

    /* Nhan tin 3: Bat buoc phai la LOW (0) */
    ipc_queue_receive(q, buf, sizeof(buf), &prio);
    TEST_ASSERT(strcmp(buf, "MSG_THAP") == 0 && prio == MSG_PRIO_LOW,
                "Tin nhan cuoi cung nhan duoc la MSG_THAP (Prio 0)");

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Test 5: Timed Receive with Timeout
 * ========================================================================= */
static void test_receive_timeout(void) {
    printf(COLOR_BOLD "\n--- Chay Test 5: Kiem tra Timeout khi hang doi rong (Timed Receive) ---\n" COLOR_RESET);
    cleanup_test_queue();

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, NULL);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue rong");

    char buf[IPC_DEFAULT_MSG_SIZE];
    unsigned int prio = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Cho 300ms tren queue rong */
    unsigned int timeout_ms = 300;
    ssize_t rc = ipc_queue_receive_timeout(q, buf, sizeof(buf), &prio, timeout_ms);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;

    TEST_ASSERT(rc == -1 && errno == ETIMEDOUT,
                "Hao ETIMEDOUT chinh xac khi het han timeout 300ms");
    TEST_ASSERT(elapsed_ms >= 280 && elapsed_ms <= 400,
                "Thoi gian cho thuc te ~300ms (do duoc tuong thich)");

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Test 6: Non-blocking Mode and Queue Full (EAGAIN)
 * ========================================================================= */
static void test_nonblocking_queue_full(void) {
    printf(COLOR_BOLD "\n--- Chay Test 6: Che do Non-blocking va xu ly Queue day (EAGAIN) ---\n" COLOR_RESET);
    cleanup_test_queue();

    QueueConfig cfg = {
        .max_messages = 3,
        .max_msg_size = 64,
        .is_nonblocking = 1
    };

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, &cfg);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue non-blocking voi dung luong toi da 3 messages");

    /* Nap 3 tin nhan vao queue */
    ipc_queue_send(q, "MSG_1", 5, MSG_PRIO_NORMAL);
    ipc_queue_send(q, "MSG_2", 5, MSG_PRIO_NORMAL);
    ipc_queue_send(q, "MSG_3", 5, MSG_PRIO_NORMAL);
    TEST_ASSERT(ipc_queue_get_curmsgs(q) == 3, "Nap day 3/3 tin nhan vao queue");

    /* Gui tin thu 4 vao queue da day o che do non-blocking -> phai tra ve EAGAIN */
    int send_rc = ipc_queue_send(q, "MSG_4_OVERFLOW", 14, MSG_PRIO_NORMAL);
    TEST_ASSERT(send_rc == -1 && errno == EAGAIN,
                "Tu choi gui va tra ve ngay lap tuc EAGAIN khi queue day o che do Non-blocking");

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Test 7: Asynchronous Notification (mq_notify with SIGUSR1)
 * ========================================================================= */
static volatile sig_atomic_t g_signal_received = 0;

static void sigusr1_handler(int sig) {
    if (sig == SIGUSR1) {
        g_signal_received = 1;
    }
}

static void test_async_notification_signal(void) {
    printf(COLOR_BOLD "\n--- Chay Test 7: Co che Bat dong bo Asynchronous Notification (mq_notify) ---\n" COLOR_RESET);
    cleanup_test_queue();

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, NULL);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue cho async notification");

    /* Dang ky signal handler cho SIGUSR1 */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigaction(SIGUSR1, &sa, NULL);

    /* Dang ky mq_notify voi SIGUSR1 */
    int notify_rc = ipc_queue_register_notify_signal(q, SIGUSR1);
    TEST_ASSERT(notify_rc == 0, "Dang ky mq_notify thanh cong voi SIGUSR1");

    /* Gui mot tin nhan vao queue dang rong */
    g_signal_received = 0;
    ipc_queue_send(q, "WAKE_UP_SIGNAL", 14, MSG_PRIO_HIGH);

    /* Cho signal duoc deliver (trong vong vai millisecond) */
    usleep(50000); // 50ms

    TEST_ASSERT(g_signal_received == 1,
                "Tien trinh nhan duoc SIGUSR1 khi co tin nhan moi vao queue rong");

    /* Doc tin nhan de giai phong queue */
    char buf[IPC_DEFAULT_MSG_SIZE];
    ipc_queue_receive(q, buf, sizeof(buf), NULL);

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Test 8: Error Strings & Safety Bounds
 * ========================================================================= */
static void test_error_handling_and_strerror(void) {
    printf(COLOR_BOLD "\n--- Chay Test 8: Thong bao loi & Gioi han an toan bo nho ---\n" COLOR_RESET);
    cleanup_test_queue();

    mqd_t q = ipc_queue_open(TEST_QUEUE_NAME, O_RDWR | O_CREAT, 0660, NULL);
    TEST_ASSERT(q != (mqd_t)-1, "Mo queue kiem tra an toan");

    /* Buffer nhan qua nho (< mq_msgsize) -> phai tu choi ngay voi EMSGSIZE */
    char tiny_buf[10];
    ssize_t rc = ipc_queue_receive(q, tiny_buf, sizeof(tiny_buf), NULL);
    TEST_ASSERT(rc == -1 && errno == EMSGSIZE,
                "Tu choi nhan khi buffer caller nho hon max_msgsize (chong Buffer Overflow)");

    /* Kiem tra mo ta loi tieng Viet/Anh ro rang */
    const char *err_timeout = ipc_queue_strerror(ETIMEDOUT);
    TEST_ASSERT(strstr(err_timeout, "timeout") != NULL, "Mo ta loi ETIMEDOUT ro rang de hieu");

    const char *err_exist = ipc_queue_strerror(EEXIST);
    TEST_ASSERT(strstr(err_exist, "da ton tai") != NULL, "Mo ta loi EEXIST ro rang de hieu");

    ipc_queue_close(q);
    cleanup_test_queue();
}

/* =========================================================================
 * Main Runner
 * ========================================================================= */
int main(void) {
    printf(COLOR_CYAN COLOR_BOLD);
    printf("=================================================================\n");
    printf("       BO TEST CHUYEN SAU IPC CORE ENGINE - THANH VIEN 1         \n");
    printf("       Tac gia: VanHuy | Ngon ngu: C POSIX 2008                 \n");
    printf("=================================================================\n");
    printf(COLOR_RESET);

    test_validate_queue_name();
    test_create_open_close_unlink();
    test_send_receive_basic();
    test_message_priority();
    test_receive_timeout();
    test_nonblocking_queue_full();
    test_async_notification_signal();
    test_error_handling_and_strerror();

    printf("\n=================================================================\n");
    printf(COLOR_BOLD "KET QUA KIEM THU:\n" COLOR_RESET);
    printf("  Tong so bai test: %d\n", g_tests_passed + g_tests_failed);
    printf("  " COLOR_GREEN "PASSED: %d" COLOR_RESET "\n", g_tests_passed);
    if (g_tests_failed > 0) {
        printf("  " COLOR_RED "FAILED: %d" COLOR_RESET "\n", g_tests_failed);
    } else {
        printf("  " COLOR_GREEN COLOR_BOLD "TAT CA CAC BAI TEST DA DAT CHUAN 100%%!" COLOR_RESET "\n");
    }
    printf("=================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
