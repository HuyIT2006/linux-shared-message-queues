#ifndef IPC_QUEUE_H
#define IPC_QUEUE_H

/**
 * @file ipc_queue.h
 * @brief POSIX Message Queue Abstraction Layer for Linux IPC
 * @author VanHuy (Thanh vien 1 - IPC Core Engine)
 *
 * Provides a robust, memory-safe wrapper around POSIX mqueue APIs with support for:
 * - Message Priority levels (LOW, NORMAL, HIGH, URGENT)
 * - Blocking and Non-blocking I/O
 * - Millisecond-precision Timed Send / Receive
 * - Asynchronous Notifications (Signal & Thread based via mq_notify)
 * - Queue Inspection and Attribute Discovery
 * - Comprehensive Error Reporting and Resource Cleanup
 */

#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants and Default Configurations
 * ========================================================================= */
#define IPC_DEFAULT_MAX_MSG         10      /**< Maximum number of messages in queue */
#define IPC_DEFAULT_MSG_SIZE        256     /**< Maximum size of each message in bytes */
#define IPC_QUEUE_NAME_MAX          64      /**< Maximum length for queue names */

#define IPC_REQUEST_QUEUE_NAME      "/shared_requests"
#define IPC_RESPONSE_QUEUE_PREFIX   "/shared_responses_"

/* Standard file mode permissions for created queues (read/write for user & group) */
#define IPC_QUEUE_DEFAULT_MODE      0660

/* =========================================================================
 * Enumerations
 * ========================================================================= */

/**
 * @brief Standardized Message Priorities.
 * Linux POSIX message queues sort messages by priority in descending order.
 * Higher numerical values are dequeued before lower numerical values.
 */
typedef enum {
    MSG_PRIO_LOW    = 0,    /**< Low priority / background tasks */
    MSG_PRIO_NORMAL = 10,   /**< Standard requests */
    MSG_PRIO_HIGH   = 20,   /**< High priority / expedited commands */
    MSG_PRIO_URGENT = 30    /**< Critical / administrative / heartbeat commands */
} MessagePriority;

/**
 * @brief Queue configuration parameters used upon queue creation.
 */
typedef struct {
    long max_messages;       /**< Maximum capacity in messages (default: 10) */
    long max_msg_size;       /**< Maximum message size in bytes (default: 256) */
    int is_nonblocking;      /**< 1: non-blocking (O_NONBLOCK), 0: blocking */
} QueueConfig;

/* =========================================================================
 * Queue Lifecycle Management
 * ========================================================================= */

/**
 * @brief Open or create a POSIX message queue with safety checks.
 *
 * @param name Queue name. Must start with '/' and contain no other slashes.
 * @param oflag Combination of O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_NONBLOCK.
 * @param mode File mode permissions when O_CREAT is used (e.g., 0660).
 * @param config Pointer to QueueConfig struct (can be NULL to use defaults).
 * @return mqd_t Valid queue descriptor on success, or (mqd_t)-1 on failure (errno is set).
 */
mqd_t ipc_queue_open(const char *name, int oflag, mode_t mode, const QueueConfig *config);

/**
 * @brief Close an open queue descriptor.
 *
 * @param mqdes Queue descriptor returned by ipc_queue_open().
 * @return 0 on success, -1 on failure (errno is set).
 */
int ipc_queue_close(mqd_t mqdes);

/**
 * @brief Remove (unlink) a message queue from the operating system.
 * The queue is destroyed once all open descriptors to it are closed.
 *
 * @param name Name of the queue to remove.
 * @return 0 on success, -1 on failure (errno is set).
 */
int ipc_queue_unlink(const char *name);

/* =========================================================================
 * Message Transmission (Send / Receive)
 * ========================================================================= */

/**
 * @brief Send a message to the queue (blocking or non-blocking depending on open flags).
 *
 * @param mqdes Queue descriptor open for writing.
 * @param msg_ptr Pointer to the message buffer.
 * @param msg_len Length of the message in bytes (must be <= queue max_msg_size).
 * @param prio Priority level (0 - 32767). Use MessagePriority enum.
 * @return 0 on success, -1 on failure (errno is set).
 */
int ipc_queue_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, MessagePriority prio);

/**
 * @brief Send a message to the queue with a millisecond timeout.
 * Blocks until space is available or until timeout_ms expires.
 *
 * @param mqdes Queue descriptor open for writing.
 * @param msg_ptr Pointer to the message buffer.
 * @param msg_len Length of the message in bytes.
 * @param prio Priority level.
 * @param timeout_ms Timeout duration in milliseconds.
 * @return 0 on success, -1 on failure (errno is set to ETIMEDOUT, etc.).
 */
int ipc_queue_send_timeout(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                           MessagePriority prio, unsigned int timeout_ms);

/**
 * @brief Receive a message from the queue (blocking).
 * Automatically ensures null-termination if the buffer has capacity.
 *
 * @param mqdes Queue descriptor open for reading.
 * @param msg_ptr Buffer to store the received message. Must be >= max_msg_size.
 * @param msg_len Size of msg_ptr buffer in bytes.
 * @param prio Optional pointer to store the priority of the received message (can be NULL).
 * @return Number of bytes received on success, -1 on failure (errno is set).
 */
ssize_t ipc_queue_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *prio);

/**
 * @brief Receive a message with a millisecond timeout.
 *
 * @param mqdes Queue descriptor open for reading.
 * @param msg_ptr Buffer to store the received message.
 * @param msg_len Size of msg_ptr buffer in bytes.
 * @param prio Optional pointer to store received priority (can be NULL).
 * @param timeout_ms Timeout duration in milliseconds.
 * @return Number of bytes received on success, -1 on failure (errno == ETIMEDOUT on timeout).
 */
ssize_t ipc_queue_receive_timeout(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                                  unsigned int *prio, unsigned int timeout_ms);

/* =========================================================================
 * Queue Attributes & Inspection
 * ========================================================================= */

/**
 * @brief Retrieve current attributes of the message queue.
 *
 * @param mqdes Queue descriptor.
 * @param attr Pointer to struct mq_attr to fill.
 * @return 0 on success, -1 on failure.
 */
int ipc_queue_get_attr(mqd_t mqdes, struct mq_attr *attr);

/**
 * @brief Get the current number of messages waiting in the queue.
 *
 * @param mqdes Queue descriptor.
 * @return Current message count on success, -1 on failure.
 */
long ipc_queue_get_curmsgs(mqd_t mqdes);

/* =========================================================================
 * Asynchronous Notification (mq_notify)
 * ========================================================================= */

/**
 * @brief Register for signal notification when a message arrives at an empty queue.
 * Note: A registration is only valid for ONE notification. It must be re-registered
 * inside the signal handler if continuous notification is desired.
 *
 * @param mqdes Queue descriptor.
 * @param signum Signal number to deliver (e.g. SIGUSR1).
 * @return 0 on success, -1 on failure.
 */
int ipc_queue_register_notify_signal(mqd_t mqdes, int signum);

/**
 * @brief Register for thread callback notification when a message arrives at an empty queue.
 *
 * @param mqdes Queue descriptor.
 * @param notify_function Function pointer called in a new thread upon message arrival.
 * @return 0 on success, -1 on failure.
 */
int ipc_queue_register_notify_thread(mqd_t mqdes, void (*notify_function)(union sigval));

/**
 * @brief Unregister any previously registered notification for this queue.
 *
 * @param mqdes Queue descriptor.
 * @return 0 on success, -1 on failure.
 */
int ipc_queue_unregister_notify(mqd_t mqdes);

/* =========================================================================
 * Helper & Validation Utilities
 * ========================================================================= */

/**
 * @brief Validate POSIX queue name formatting rules.
 *
 * @param name Queue name string.
 * @return 0 if valid, -1 if invalid (errno is set to EINVAL).
 */
int ipc_queue_validate_name(const char *name);

/**
 * @brief Format client-specific response queue name: /shared_responses_<PID>.
 *
 * @param dest Output buffer.
 * @param dest_size Size of output buffer.
 * @param pid Process ID of the client.
 * @return 0 on success, -1 on buffer overflow.
 */
int ipc_queue_build_response_name(char *dest, size_t dest_size, pid_t pid);

/**
 * @brief Return a descriptive, human-readable error string for an IPC error.
 *
 * @param err_num errno code (e.g. ETIMEDOUT, EAGAIN, EEXIST, EMSGSIZE).
 * @return Descriptive string explanation.
 */
const char *ipc_queue_strerror(int err_num);

#ifdef __cplusplus
}
#endif

#endif /* IPC_QUEUE_H */
