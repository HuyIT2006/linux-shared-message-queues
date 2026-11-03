#ifndef WORKER_POOL_H
#define WORKER_POOL_H

/**
 * @file worker_pool.h
 * @brief Multi-Threaded Worker Pool Engine for Linux IPC Server
 * @author Thanh vien 2 (Server Architecture, Worker Pool & System Services)
 *
 * Implements a thread-safe, priority-ordered job queue and POSIX pthreads
 * worker pool. Decouples request reception on /shared_requests from command
 * execution and response transmission to /shared_responses_<PID>.
 */

#include "ipc_queue.h"
#include "protocol.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Default Worker Pool Constants
 * ========================================================================= */
#define WORKER_POOL_DEFAULT_THREADS     4       /**< Default number of worker threads */
#define WORKER_POOL_MAX_THREADS         64      /**< Maximum allowed worker threads */
#define WORKER_POOL_DEFAULT_CAPACITY    128     /**< Default internal job queue capacity */
#define WORKER_POOL_SEND_TIMEOUT_MS     2000    /**< Default timeout sending response to client */

/* =========================================================================
 * Callback & Configuration Types
 * ========================================================================= */

/**
 * @brief Optional custom callback for delivering a serialized response.
 * If NULL in WorkerPoolConfig, the worker sends the response over POSIX mqueue
 * to `/shared_responses_<client_pid>`.
 *
 * @param client_pid Destination client PID.
 * @param response_str Null-terminated serialized response string.
 * @param prio Message priority.
 * @param user_data Opaque user pointer from WorkerPoolConfig.
 * @return 0 on success, -1 on failure.
 */
typedef int (*WorkerResponseCallback)(pid_t client_pid, const char *response_str,
                                      MessagePriority prio, void *user_data);

/**
 * @brief Configuration parameters for creating a WorkerPool instance.
 */
typedef struct {
    int num_threads;                        /**< Number of worker pthreads (default: 4) */
    int max_queue_size;                     /**< Max pending jobs in internal queue (default: 128) */
    unsigned int send_timeout_ms;           /**< Timeout in ms for mq_timedsend to client queue */
    ProtoFormatMode format_mode;            /**< Response format mode (AUTO, BASIC, EXTENDED) */
    bool verbose_log;                       /**< Print detailed worker execution logs */
    WorkerResponseCallback response_cb;     /**< Optional override for response delivery */
    void *user_data;                        /**< Opaque context passed to response_cb */
} WorkerPoolConfig;

/**
 * @brief Opaque handle to a WorkerPool instance.
 */
typedef struct WorkerPool WorkerPool;

/* =========================================================================
 * Worker Pool Lifecycle & Submission APIs
 * ========================================================================= */

/**
 * @brief Create and start a multi-threaded worker pool.
 *
 * @param config Pointer to configuration (can be NULL for defaults).
 * @return Pointer to initialized WorkerPool, or NULL on error.
 */
WorkerPool *worker_pool_create(const WorkerPoolConfig *config);

/**
 * @brief Submit a raw message received from `/shared_requests` into the priority job queue.
 *
 * If the raw message has a valid `<PID>|...` prefix, it is queued according to its
 * effective priority (`URGENT` > `HIGH` > `NORMAL` > `LOW`) and processed asynchronously
 * by a worker thread.
 *
 * @param pool Active WorkerPool instance.
 * @param raw_msg Raw request buffer.
 * @param msg_len Length of raw_msg in bytes.
 * @param mq_prio Priority value returned by mq_receive().
 * @return 0 on successful enqueue, -1 if pool is shutting down or queue is full.
 */
int worker_pool_submit(WorkerPool *pool, const char *raw_msg, size_t msg_len, unsigned int mq_prio);

/**
 * @brief Wait until all currently queued and active jobs have completed.
 *
 * @param pool Active WorkerPool instance.
 * @return 0 on success, -1 on invalid pool.
 */
int worker_pool_wait_idle(WorkerPool *pool);

/**
 * @brief Retrieve a thread-safe snapshot of server and worker pool statistics.
 *
 * @param pool Active WorkerPool instance.
 * @param out_stats Output snapshot structure.
 * @return 0 on success, -1 on error.
 */
int worker_pool_get_stats(WorkerPool *pool, ServerStatsSnapshot *out_stats);

/**
 * @brief Gracefully shut down the worker pool, joining all worker threads and freeing memory.
 *
 * @param pool WorkerPool instance to destroy.
 * @param drain_queue If true, workers finish all pending jobs before exiting.
 */
void worker_pool_destroy(WorkerPool *pool, bool drain_queue);

#ifdef __cplusplus
}
#endif

#endif /* WORKER_POOL_H */