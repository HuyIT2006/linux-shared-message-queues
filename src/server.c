/**
 * @file server.c
 * @brief Multi-Threaded Central IPC Server for Linux Shared Message Queues
 * @author Thanh vien 2 (Server Architecture, Worker Pool & System Services)
 *
 * Listens continuously on `/shared_requests`, dispatches incoming client requests
 * to a multi-threaded POSIX pthreads Worker Pool, executes system services
 * (PING, TIME, FIB, UPPER, SYSINFO, STATS, ECHO, CALC), routes responses to
 * `/shared_responses_<PID>`, and handles SIGINT/SIGTERM for graceful shutdown.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "ipc_queue.h"
#include "protocol.h"
#include "worker_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

/* Global atomic flag for graceful shutdown on SIGINT / SIGTERM */
static volatile sig_atomic_t g_server_running = 1;
static volatile sig_atomic_t g_last_signal = 0;

static void signal_handler(int signum) {
    g_last_signal = signum;
    g_server_running = 0;
}

static void setup_signal_handlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    /* Do not set SA_RESTART so mq_receive / mq_timedreceive wakes up on signal */
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

static void print_banner(int num_workers, int max_queue, ProtoFormatMode mode) {
    const char *mode_str = (mode == PROTO_MODE_EXTENDED) ? "EXTENDED (<STATUS>|<MS>|<PAYLOAD>)" :
                           (mode == PROTO_MODE_BASIC)    ? "BASIC (<STATUS>|<MESSAGE>)" :
                                                           "AUTO (Tuong thich 3-field & 4-field)";
    printf(COLOR_CYAN COLOR_BOLD);
    printf("=================================================================\n");
    printf("     LINUX IPC MULTI-THREADED MESSAGE QUEUE SERVER (TV2)         \n");
    printf("=================================================================\n");
    printf(COLOR_RESET);
    printf("  Request Queue   : " COLOR_GREEN "%s" COLOR_RESET "\n", IPC_REQUEST_QUEUE_NAME);
    printf("  Worker Threads  : " COLOR_BOLD "%d threads" COLOR_RESET "\n", num_workers);
    printf("  Internal Queue  : " COLOR_BOLD "%d jobs max" COLOR_RESET "\n", max_queue);
    printf("  Protocol Mode   : %s\n", mode_str);
    printf("  Commands        : PING, TIME, FIB, UPPER, SYSINFO, STATS, ECHO, CALC\n");
    printf("  Nhan " COLOR_YELLOW "Ctrl+C (SIGINT)" COLOR_RESET " de Graceful Shutdown va xoa queue.\n");
    printf("-----------------------------------------------------------------\n");
}

static void print_usage(const char *prog_name) {
    printf("Su dung: %s [TUY CHON]\n\n", prog_name);
    printf("Cac tuy chon:\n");
    printf("  -w, --workers <N>       So luong worker threads trong pool (mac dinh: 4)\n");
    printf("  -q, --queue-size <M>    Suc chua hang doi cong viec noi bo (mac dinh: 128)\n");
    printf("  -t, --timeout <MS>      Timeout khi gui response ve client (mac dinh: 2000 ms)\n");
    printf("  -m, --mode <MODE>       Che do format response: auto | basic | extended (mac dinh: auto)\n");
    printf("  -n, --max-requests <K>  Xu ly dung K requests roi tu dong dung (dung cho test tu dong)\n");
    printf("  -q-quiet, --quiet       Tat log chi tiet tung request\n");
    printf("  -h, --help              Hien thi huong dan su dung\n");
}

int main(int argc, char *argv[]) {
    WorkerPoolConfig pool_cfg = {
        .num_threads = WORKER_POOL_DEFAULT_THREADS,
        .max_queue_size = WORKER_POOL_DEFAULT_CAPACITY,
        .send_timeout_ms = WORKER_POOL_SEND_TIMEOUT_MS,
        .format_mode = PROTO_MODE_AUTO,
        .verbose_log = true,
        .response_cb = NULL,
        .user_data = NULL
    };

    long max_requests_limit = 0; /* 0 = unlimited */

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--workers") == 0) && i + 1 < argc) {
            int w = atoi(argv[++i]);
            if (w < 1 || w > WORKER_POOL_MAX_THREADS) {
                fprintf(stderr, COLOR_RED "Loi: So worker threads phai tu 1 den %d.\n" COLOR_RESET,
                        WORKER_POOL_MAX_THREADS);
                return 1;
            }
            pool_cfg.num_threads = w;
        } else if ((strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--queue-size") == 0) && i + 1 < argc) {
            int q = atoi(argv[++i]);
            if (q < 1) {
                fprintf(stderr, COLOR_RED "Loi: Kich thuoc queue noi bo phai >= 1.\n" COLOR_RESET);
                return 1;
            }
            pool_cfg.max_queue_size = q;
        } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1 < argc) {
            int t = atoi(argv[++i]);
            if (t < 10) {
                fprintf(stderr, COLOR_RED "Loi: Timeout phai >= 10 ms.\n" COLOR_RESET);
                return 1;
            }
            pool_cfg.send_timeout_ms = (unsigned int)t;
        } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "basic") == 0) {
                pool_cfg.format_mode = PROTO_MODE_BASIC;
            } else if (strcmp(m, "extended") == 0) {
                pool_cfg.format_mode = PROTO_MODE_EXTENDED;
            } else if (strcmp(m, "auto") == 0) {
                pool_cfg.format_mode = PROTO_MODE_AUTO;
            } else {
                fprintf(stderr, COLOR_RED "Loi: Mode khong hop le (%s). Chon auto, basic, hoac extended.\n" COLOR_RESET, m);
                return 1;
            }
        } else if ((strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--max-requests") == 0) && i + 1 < argc) {
            max_requests_limit = atol(argv[++i]);
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q-quiet") == 0) {
            pool_cfg.verbose_log = false;
        } else {
            fprintf(stderr, COLOR_RED "Tuy chon khong hop le: %s\n" COLOR_RESET, argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    setup_signal_handlers();

    /* Remove any stale /shared_requests queue from a previous crashed run */
    ipc_queue_unlink(IPC_REQUEST_QUEUE_NAME);

    QueueConfig q_cfg = {
        .max_messages = IPC_DEFAULT_MAX_MSG,
        .max_msg_size = IPC_DEFAULT_MSG_SIZE,
        .is_nonblocking = 0
    };

    mqd_t req_mq = ipc_queue_open(IPC_REQUEST_QUEUE_NAME, O_RDONLY | O_CREAT,
                                  IPC_QUEUE_DEFAULT_MODE, &q_cfg);
    if (req_mq == (mqd_t)-1) {
        fprintf(stderr, COLOR_RED "[Server] Loi nghiem trong: Khong the tao/mo queue %s: %s\n" COLOR_RESET,
                IPC_REQUEST_QUEUE_NAME, ipc_queue_strerror(errno));
        return 1;
    }

    WorkerPool *pool = worker_pool_create(&pool_cfg);
    if (!pool) {
        fprintf(stderr, COLOR_RED "[Server] Loi: Khong the khoi tao Worker Thread Pool.\n" COLOR_RESET);
        ipc_queue_close(req_mq);
        ipc_queue_unlink(IPC_REQUEST_QUEUE_NAME);
        return 1;
    }

    print_banner(pool_cfg.num_threads, pool_cfg.max_queue_size, pool_cfg.format_mode);

    long received_count = 0;
    char recv_buf[IPC_DEFAULT_MSG_SIZE + 1];

    /* Master Listener Loop */
    while (g_server_running) {
        unsigned int msg_prio = 0;
        ssize_t bytes_read = ipc_queue_receive_timeout(req_mq, recv_buf, sizeof(recv_buf),
                                                       &msg_prio, 500);
        if (bytes_read < 0) {
            if (errno == ETIMEDOUT || errno == EINTR) {
                continue;
            }
            if (errno == ENOSYS) {
                /* Fallback to blocking receive if timedreceive is unsupported */
                bytes_read = ipc_queue_receive(req_mq, recv_buf, sizeof(recv_buf), &msg_prio);
                if (bytes_read < 0) {
                    if (errno == EINTR) continue;
                    fprintf(stderr, COLOR_RED "[Server] Loi mq_receive: %s\n" COLOR_RESET,
                            ipc_queue_strerror(errno));
                    break;
                }
            } else {
                fprintf(stderr, COLOR_RED "[Server] Loi doc queue %s: %s\n" COLOR_RESET,
                        IPC_REQUEST_QUEUE_NAME, ipc_queue_strerror(errno));
                break;
            }
        }

        if (pool_cfg.verbose_log) {
            printf(COLOR_GREEN "[Master]" COLOR_RESET " Nhan request (%zd bytes, MQ_Prio=%u): \"%s\"\n",
                   bytes_read, msg_prio, recv_buf);
        }

        if (worker_pool_submit(pool, recv_buf, (size_t)bytes_read, msg_prio) != 0) {
            fprintf(stderr, COLOR_YELLOW "[Master] Canh bao: Khong the dua job vao Worker Pool: %s\n" COLOR_RESET,
                    strerror(errno));
        }

        received_count++;
        if (max_requests_limit > 0 && received_count >= max_requests_limit) {
            printf(COLOR_YELLOW "[Master] Da nhan du %ld requests theo gioi han (--max-requests). Bat dau dung server...\n" COLOR_RESET,
                   max_requests_limit);
            break;
        }
    }

    if (g_last_signal != 0) {
        printf("\n" COLOR_YELLOW "[Server] Nhan tin hieu %d (%s). Dang thuc hien Graceful Shutdown..." COLOR_RESET "\n",
               (int)g_last_signal,
               (g_last_signal == SIGINT) ? "SIGINT" : "SIGTERM");
    }

    /* Drain all in-flight worker jobs and collect final metrics */
    worker_pool_wait_idle(pool);

    ServerStatsSnapshot final_stats;
    worker_pool_get_stats(pool, &final_stats);

    worker_pool_destroy(pool, true);

    /* Close and unlink /shared_requests */
    ipc_queue_close(req_mq);
    if (ipc_queue_unlink(IPC_REQUEST_QUEUE_NAME) == 0) {
        printf(COLOR_GREEN "[Server] Da dong va xoa queue %s thanh cong.\n" COLOR_RESET,
               IPC_REQUEST_QUEUE_NAME);
    }

    printf(COLOR_CYAN COLOR_BOLD);
    printf("-----------------------------------------------------------------\n");
    printf("THONG KE VAN HANH SERVER TRUOC KHI THOAT:\n");
    printf("  - Tong so request da xu ly : %" PRIu64 "\n", final_stats.total_requests);
    printf("  - So request loi (ERROR)   : %" PRIu64 "\n", final_stats.failed_requests);
    printf("  - Thoi gian hoat dong      : %ld giay\n", final_stats.uptime_seconds);
    printf("=================================================================\n");
    printf(COLOR_RESET);

    return 0;
}