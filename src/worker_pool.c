/**
 * @file worker_pool.c
 * @brief Implementation of Multi-Threaded Worker Pool with Priority Job Queue
 * @author Thanh vien 2 (Server Architecture, Worker Pool & System Services)
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "worker_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

/* =========================================================================
 * Internal Data Structures
 * ========================================================================= */

/**
 * @brief Node in the priority-ordered internal job queue.
 */
typedef struct JobNode {
    char raw_msg[PROTO_MAX_MSG_SIZE];
    size_t msg_len;
    unsigned int mq_prio;
    MessagePriority effective_prio;
    struct JobNode *next;
} JobNode;

/**
 * @brief Complete WorkerPool state protected by a POSIX mutex and condition variables.
 */
struct WorkerPool {
    pthread_t *threads;
    int num_threads;
    int max_queue_size;
    unsigned int send_timeout_ms;
    ProtoFormatMode format_mode;
    bool verbose_log;

    WorkerResponseCallback response_cb;
    void *user_data;

    /* Priority linked-list job queue (highest priority at head, FIFO within same priority) */
    JobNode *head;
    int queued_count;
    int active_workers;

    /* Operational metrics */
    uint64_t total_requests;
    uint64_t failed_requests;
    time_t start_time;

    /* Shutdown flags */
    bool shutting_down;
    bool immediate_stop;

    /* Synchronization primitives */
    pthread_mutex_t lock;
    pthread_cond_t cond_has_jobs;
    pthread_cond_t cond_has_space;
    pthread_cond_t cond_all_idle;
};

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

/**
 * @brief Send a serialized response string to `/shared_responses_<PID>` using IPC Core.
 */
static int send_response_via_mqueue(pid_t client_pid, const char *response_str,
                                    MessagePriority prio, unsigned int timeout_ms,
                                    bool verbose) {
    char qname[IPC_QUEUE_NAME_MAX];
    if (ipc_queue_build_response_name(qname, sizeof(qname), client_pid) != 0) {
        if (verbose) {
            fprintf(stderr, COLOR_RED "[Worker] Loi tao ten queue cho PID %d\n" COLOR_RESET,
                    (int)client_pid);
        }
        return -1;
    }

    mqd_t client_q = ipc_queue_open(qname, O_WRONLY, 0, NULL);
    if (client_q == (mqd_t)-1) {
        if (verbose) {
            fprintf(stderr, COLOR_YELLOW "[Worker] Khong the mo queue response %s: %s\n" COLOR_RESET,
                    qname, ipc_queue_strerror(errno));
        }
        return -1;
    }

    size_t len = strlen(response_str);
    int rc = ipc_queue_send_timeout(client_q, response_str, len, prio, timeout_ms);
    if (rc != 0 && errno == ENOSYS) {
        /* Fallback if mq_timedsend is not supported by kernel/seccomp */
        rc = ipc_queue_send(client_q, response_str, len, prio);
    }

    if (rc != 0 && verbose) {
        fprintf(stderr, COLOR_RED "[Worker] Loi gui response toi %s: %s\n" COLOR_RESET,
                qname, ipc_queue_strerror(errno));
    }

    ipc_queue_close(client_q);
    return rc;
}

/**
 * @brief Insert a job node into the priority linked list in descending priority order
 * (FIFO order preserved among jobs with equal priority).
 */
static void enqueue_priority_job(WorkerPool *pool, JobNode *node) {
    if (!pool->head || node->effective_prio > pool->head->effective_prio) {
        node->next = pool->head;
        pool->head = node;
    } else {
        JobNode *curr = pool->head;
        while (curr->next && curr->next->effective_prio >= node->effective_prio) {
            curr = curr->next;
        }
        node->next = curr->next;
        curr->next = node;
    }
    pool->queued_count++;
}

/**
 * @brief Pop the highest-priority job node from the queue head.
 */
static JobNode *dequeue_priority_job(WorkerPool *pool) {
    JobNode *node = pool->head;
    if (node) {
        pool->head = node->next;
        node->next = NULL;
        pool->queued_count--;
    }
    return node;
}

/**
 * @brief Quick peek at raw message to determine its effective priority for queue ordering.
 */
static MessagePriority peek_effective_priority(const char *raw_msg, size_t msg_len,
                                               unsigned int mq_prio) {
    IpcRequest temp_req;
    if (protocol_parse_request(raw_msg, msg_len, mq_prio, &temp_req) == PROTO_OK) {
        return temp_req.priority;
    }
    if (mq_prio >= MSG_PRIO_URGENT) return MSG_PRIO_URGENT;
    if (mq_prio >= MSG_PRIO_HIGH)   return MSG_PRIO_HIGH;
    if (mq_prio >= MSG_PRIO_NORMAL) return MSG_PRIO_NORMAL;
    return MSG_PRIO_LOW;
}

/**
 * @brief Attempt to extract a numeric PID prefix even from a malformed request
 * (e.g. `1234|BAD_FORMAT`) so the server can still send `ERROR|INVALID_REQUEST`
 * back to `/shared_responses_1234` whenever possible.
 */
static pid_t extract_fallback_pid(const char *raw_msg, size_t msg_len) {
    if (!raw_msg || msg_len == 0) return -1;
    char pid_buf[32];
    size_t i = 0;
    while (i < msg_len && i < sizeof(pid_buf) - 1 && raw_msg[i] != '|') {
        if (raw_msg[i] < '0' || raw_msg[i] > '9') {
            return -1;
        }
        pid_buf[i] = raw_msg[i];
        i++;
    }
    if (i == 0 || i >= msg_len || raw_msg[i] != '|') {
        return -1;
    }
    pid_buf[i] = '\0';
    long val = strtol(pid_buf, NULL, 10);
    return (val > 0) ? (pid_t)val : -1;
}

/**
 * @brief Worker thread entry point.
 */
static void *worker_thread_main(void *arg) {
    WorkerPool *pool = (WorkerPool *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->lock);

        while (pool->queued_count == 0 && !pool->shutting_down) {
            pthread_cond_wait(&pool->cond_has_jobs, &pool->lock);
        }

        if (pool->immediate_stop || (pool->shutting_down && pool->queued_count == 0)) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        JobNode *job = dequeue_priority_job(pool);
        pool->active_workers++;
        pthread_cond_signal(&pool->cond_has_space);

        /* Capture stats snapshot while holding the lock (include current request) */
        time_t now = time(NULL);
        ServerStatsSnapshot stats_snap = {
            .total_requests = pool->total_requests + 1,
            .failed_requests = pool->failed_requests,
            .active_workers = pool->active_workers,
            .total_workers = pool->num_threads,
            .queued_jobs = pool->queued_count,
            .uptime_seconds = (now >= pool->start_time) ? (long)(now - pool->start_time) : 0
        };

        pthread_mutex_unlock(&pool->lock);

        if (!job) {
            continue;
        }

        /* Parse and execute request outside the mutex lock for true parallelism */
        IpcRequest req;
        IpcResponse resp;
        memset(&resp, 0, sizeof(resp));

        ProtoErrorCode parse_rc = protocol_parse_request(job->raw_msg, job->msg_len,
                                                         job->mq_prio, &req);
        bool is_error = false;
        pid_t target_pid = -1;
        MessagePriority resp_prio = MSG_PRIO_NORMAL;
        bool include_exec_time = false;

        if (parse_rc == PROTO_OK) {
            target_pid = req.client_pid;
            resp_prio = req.priority;

            ProtoErrorCode exec_rc = protocol_execute_request(&req, &resp, &stats_snap);
            if (exec_rc != PROTO_OK) {
                is_error = true;
            }

            if (pool->format_mode == PROTO_MODE_EXTENDED) {
                include_exec_time = true;
            } else if (pool->format_mode == PROTO_MODE_BASIC) {
                include_exec_time = false;
            } else {
                include_exec_time = req.is_extended_format;
            }
        } else {
            is_error = true;
            resp.status = PROTO_STATUS_ERROR;
            resp.error_code = parse_rc;
            resp.exec_time_ms = 0.0;
            snprintf(resp.payload, sizeof(resp.payload), "%s", protocol_error_to_str(parse_rc));
            target_pid = extract_fallback_pid(job->raw_msg, job->msg_len);
            include_exec_time = (pool->format_mode == PROTO_MODE_EXTENDED);
        }

        /* Serialize and transmit response if client PID is known */
        char resp_wire[PROTO_MAX_MSG_SIZE];
        if (protocol_serialize_response(&resp, resp_wire, sizeof(resp_wire), include_exec_time) >= 0) {
            if (pool->verbose_log) {
                printf(COLOR_CYAN "[Worker]" COLOR_RESET " PID=%d | Req=\"%s\" -> Resp=\"%s\" (%.3f ms)\n",
                       (int)target_pid, job->raw_msg, resp_wire, resp.exec_time_ms);
            }

            if (target_pid > 0) {
                if (pool->response_cb) {
                    pool->response_cb(target_pid, resp_wire, resp_prio, pool->user_data);
                } else {
                    send_response_via_mqueue(target_pid, resp_wire, resp_prio,
                                             pool->send_timeout_ms, pool->verbose_log);
                }
            }
        }

        free(job);

        /* Update completion counters under lock */
        pthread_mutex_lock(&pool->lock);
        pool->total_requests++;
        if (is_error) {
            pool->failed_requests++;
        }
        pool->active_workers--;
        if (pool->queued_count == 0 && pool->active_workers == 0) {
            pthread_cond_broadcast(&pool->cond_all_idle);
        }
        pthread_mutex_unlock(&pool->lock);
    }

    return NULL;
}

/* =========================================================================
 * Public API Implementations
 * ========================================================================= */

WorkerPool *worker_pool_create(const WorkerPoolConfig *config) {
    WorkerPool *pool = (WorkerPool *)calloc(1, sizeof(WorkerPool));
    if (!pool) {
        return NULL;
    }

    pool->num_threads = WORKER_POOL_DEFAULT_THREADS;
    pool->max_queue_size = WORKER_POOL_DEFAULT_CAPACITY;
    pool->send_timeout_ms = WORKER_POOL_SEND_TIMEOUT_MS;
    pool->format_mode = PROTO_MODE_AUTO;
    pool->verbose_log = false;
    pool->response_cb = NULL;
    pool->user_data = NULL;

    if (config) {
        if (config->num_threads > 0 && config->num_threads <= WORKER_POOL_MAX_THREADS) {
            pool->num_threads = config->num_threads;
        }
        if (config->max_queue_size > 0) {
            pool->max_queue_size = config->max_queue_size;
        }
        if (config->send_timeout_ms > 0) {
            pool->send_timeout_ms = config->send_timeout_ms;
        }
        pool->format_mode = config->format_mode;
        pool->verbose_log = config->verbose_log;
        pool->response_cb = config->response_cb;
        pool->user_data = config->user_data;
    }

    pool->start_time = time(NULL);

    if (pthread_mutex_init(&pool->lock, NULL) != 0) {
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->cond_has_jobs, NULL) != 0) {
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->cond_has_space, NULL) != 0) {
        pthread_cond_destroy(&pool->cond_has_jobs);
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->cond_all_idle, NULL) != 0) {
        pthread_cond_destroy(&pool->cond_has_space);
        pthread_cond_destroy(&pool->cond_has_jobs);
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t *)calloc((size_t)pool->num_threads, sizeof(pthread_t));
    if (!pool->threads) {
        pthread_cond_destroy(&pool->cond_all_idle);
        pthread_cond_destroy(&pool->cond_has_space);
        pthread_cond_destroy(&pool->cond_has_jobs);
        pthread_mutex_destroy(&pool->lock);
        free(pool);
        return NULL;
    }

    int started = 0;
    for (int i = 0; i < pool->num_threads; ++i) {
        if (pthread_create(&pool->threads[i], NULL, worker_thread_main, pool) != 0) {
            pthread_mutex_lock(&pool->lock);
            pool->shutting_down = true;
            pool->immediate_stop = true;
            pthread_cond_broadcast(&pool->cond_has_jobs);
            pthread_mutex_unlock(&pool->lock);

            for (int j = 0; j < started; ++j) {
                pthread_join(pool->threads[j], NULL);
            }
            free(pool->threads);
            pthread_cond_destroy(&pool->cond_all_idle);
            pthread_cond_destroy(&pool->cond_has_space);
            pthread_cond_destroy(&pool->cond_has_jobs);
            pthread_mutex_destroy(&pool->lock);
            free(pool);
            return NULL;
        }
        started++;
    }

    return pool;
}

int worker_pool_submit(WorkerPool *pool, const char *raw_msg, size_t msg_len, unsigned int mq_prio) {
    if (!pool || !raw_msg || msg_len == 0 || msg_len >= PROTO_MAX_MSG_SIZE) {
        errno = EINVAL;
        return -1;
    }

    JobNode *node = (JobNode *)calloc(1, sizeof(JobNode));
    if (!node) {
        errno = ENOMEM;
        return -1;
    }

    memcpy(node->raw_msg, raw_msg, msg_len);
    node->raw_msg[msg_len] = '\0';
    node->msg_len = msg_len;
    node->mq_prio = mq_prio;
    node->effective_prio = peek_effective_priority(raw_msg, msg_len, mq_prio);
    node->next = NULL;

    pthread_mutex_lock(&pool->lock);

    while (pool->queued_count >= pool->max_queue_size && !pool->shutting_down) {
        pthread_cond_wait(&pool->cond_has_space, &pool->lock);
    }

    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->lock);
        free(node);
        errno = ESHUTDOWN;
        return -1;
    }

    enqueue_priority_job(pool, node);
    pthread_cond_signal(&pool->cond_has_jobs);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

int worker_pool_wait_idle(WorkerPool *pool) {
    if (!pool) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&pool->lock);
    while (pool->queued_count > 0 || pool->active_workers > 0) {
        pthread_cond_wait(&pool->cond_all_idle, &pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

int worker_pool_get_stats(WorkerPool *pool, ServerStatsSnapshot *out_stats) {
    if (!pool || !out_stats) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&pool->lock);
    time_t now = time(NULL);
    out_stats->total_requests = pool->total_requests;
    out_stats->failed_requests = pool->failed_requests;
    out_stats->active_workers = pool->active_workers;
    out_stats->total_workers = pool->num_threads;
    out_stats->queued_jobs = pool->queued_count;
    out_stats->uptime_seconds = (now >= pool->start_time) ? (long)(now - pool->start_time) : 0;
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

void worker_pool_destroy(WorkerPool *pool, bool drain_queue) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->lock);
    pool->shutting_down = true;
    pool->immediate_stop = !drain_queue;
    pthread_cond_broadcast(&pool->cond_has_jobs);
    pthread_cond_broadcast(&pool->cond_has_space);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_threads; ++i) {
        pthread_join(pool->threads[i], NULL);
    }

    /* Free any remaining job nodes if immediate_stop was requested */
    while (pool->head) {
        JobNode *tmp = pool->head;
        pool->head = tmp->next;
        free(tmp);
    }

    free(pool->threads);
    pthread_cond_destroy(&pool->cond_all_idle);
    pthread_cond_destroy(&pool->cond_has_space);
    pthread_cond_destroy(&pool->cond_has_jobs);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}