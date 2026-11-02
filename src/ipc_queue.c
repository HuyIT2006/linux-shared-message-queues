/**
 * @file ipc_queue.c
 * @brief Implementation of POSIX Message Queue Abstraction Layer
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

/* =========================================================================
 * Internal Helpers
 * ========================================================================= */

/**
 * @brief Convert a relative millisecond duration into an absolute timespec
 * based on CLOCK_REALTIME (required by mq_timedsend / mq_timedreceive).
 */
static int calculate_abs_timespec(unsigned int timeout_ms, struct timespec *abs_ts) {
    if (!abs_ts) {
        errno = EINVAL;
        return -1;
    }

    if (clock_gettime(CLOCK_REALTIME, abs_ts) != 0) {
        return -1;
    }

    time_t sec_delta = (time_t)(timeout_ms / 1000);
    long nsec_delta = (long)((timeout_ms % 1000) * 1000000L);

    abs_ts->tv_sec += sec_delta;
    abs_ts->tv_nsec += nsec_delta;

    if (abs_ts->tv_nsec >= 1000000000L) {
        abs_ts->tv_sec += 1;
        abs_ts->tv_nsec -= 1000000000L;
    }

    return 0;
}

/* =========================================================================
 * Public API Implementations
 * ========================================================================= */

int ipc_queue_validate_name(const char *name) {
    if (!name) {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(name);
    if (len < 2 || len >= IPC_QUEUE_NAME_MAX) {
        errno = EINVAL;
        return -1;
    }

    /* POSIX rule: Must start with a slash */
    if (name[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    /* POSIX rule: Must contain no other slashes */
    for (size_t i = 1; i < len; ++i) {
        if (name[i] == '/') {
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

int ipc_queue_build_response_name(char *dest, size_t dest_size, pid_t pid) {
    if (!dest || dest_size < 32 || pid <= 0) {
        errno = EINVAL;
        return -1;
    }

    int written = snprintf(dest, dest_size, "%s%d", IPC_RESPONSE_QUEUE_PREFIX, (int)pid);
    if (written < 0 || (size_t)written >= dest_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

mqd_t ipc_queue_open(const char *name, int oflag, mode_t mode, const QueueConfig *config) {
    if (ipc_queue_validate_name(name) != 0) {
        return (mqd_t)-1;
    }

    if (oflag & O_CREAT) {
        struct mq_attr attr;
        memset(&attr, 0, sizeof(attr));

        if (config) {
            attr.mq_maxmsg = (config->max_messages > 0) ? config->max_messages : IPC_DEFAULT_MAX_MSG;
            attr.mq_msgsize = (config->max_msg_size > 0) ? config->max_msg_size : IPC_DEFAULT_MSG_SIZE;
            if (config->is_nonblocking) {
                oflag |= O_NONBLOCK;
            }
        } else {
            attr.mq_maxmsg = IPC_DEFAULT_MAX_MSG;
            attr.mq_msgsize = IPC_DEFAULT_MSG_SIZE;
        }

        mode_t target_mode = (mode != 0) ? mode : IPC_QUEUE_DEFAULT_MODE;
        return mq_open(name, oflag, target_mode, &attr);
    }

    return mq_open(name, oflag);
}

int ipc_queue_close(mqd_t mqdes) {
    if (mqdes == (mqd_t)-1) {
        errno = EBADF;
        return -1;
    }
    return mq_close(mqdes);
}

int ipc_queue_unlink(const char *name) {
    if (ipc_queue_validate_name(name) != 0) {
        return -1;
    }
    return mq_unlink(name);
}

int ipc_queue_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, MessagePriority prio) {
    if (mqdes == (mqd_t)-1 || !msg_ptr) {
        errno = EINVAL;
        return -1;
    }
    return mq_send(mqdes, msg_ptr, msg_len, (unsigned int)prio);
}

int ipc_queue_send_timeout(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                           MessagePriority prio, unsigned int timeout_ms) {
    if (mqdes == (mqd_t)-1 || !msg_ptr) {
        errno = EINVAL;
        return -1;
    }

    struct timespec abs_ts;
    if (calculate_abs_timespec(timeout_ms, &abs_ts) != 0) {
        return -1;
    }

    return mq_timedsend(mqdes, msg_ptr, msg_len, (unsigned int)prio, &abs_ts);
}

ssize_t ipc_queue_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *prio) {
    if (mqdes == (mqd_t)-1 || !msg_ptr || msg_len == 0) {
        errno = EINVAL;
        return -1;
    }

    /* POSIX requirement: buffer size must be >= mq_msgsize */
    struct mq_attr attr;
    if (mq_getattr(mqdes, &attr) == 0) {
        if ((long)msg_len < attr.mq_msgsize) {
            errno = EMSGSIZE;
            return -1;
        }
    }

    ssize_t bytes_read = mq_receive(mqdes, msg_ptr, msg_len, prio);
    if (bytes_read >= 0 && (size_t)bytes_read < msg_len) {
        msg_ptr[bytes_read] = '\0';
    }

    return bytes_read;
}

ssize_t ipc_queue_receive_timeout(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                                  unsigned int *prio, unsigned int timeout_ms) {
    if (mqdes == (mqd_t)-1 || !msg_ptr || msg_len == 0) {
        errno = EINVAL;
        return -1;
    }

    struct mq_attr attr;
    if (mq_getattr(mqdes, &attr) == 0) {
        if ((long)msg_len < attr.mq_msgsize) {
            errno = EMSGSIZE;
            return -1;
        }
    }

    struct timespec abs_ts;
    if (calculate_abs_timespec(timeout_ms, &abs_ts) != 0) {
        return -1;
    }

    ssize_t bytes_read = mq_timedreceive(mqdes, msg_ptr, msg_len, prio, &abs_ts);
    if (bytes_read >= 0 && (size_t)bytes_read < msg_len) {
        msg_ptr[bytes_read] = '\0';
    }

    return bytes_read;
}

int ipc_queue_get_attr(mqd_t mqdes, struct mq_attr *attr) {
    if (mqdes == (mqd_t)-1 || !attr) {
        errno = EINVAL;
        return -1;
    }
    return mq_getattr(mqdes, attr);
}

long ipc_queue_get_curmsgs(mqd_t mqdes) {
    struct mq_attr attr;
    if (ipc_queue_get_attr(mqdes, &attr) != 0) {
        return -1;
    }
    return attr.mq_curmsgs;
}

int ipc_queue_register_notify_signal(mqd_t mqdes, int signum) {
    if (mqdes == (mqd_t)-1 || signum <= 0) {
        errno = EINVAL;
        return -1;
    }

    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = signum;

    return mq_notify(mqdes, &sev);
}

int ipc_queue_register_notify_thread(mqd_t mqdes, void (*notify_function)(union sigval)) {
    if (mqdes == (mqd_t)-1 || !notify_function) {
        errno = EINVAL;
        return -1;
    }

    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = notify_function;
    sev.sigev_notify_attributes = NULL;

    return mq_notify(mqdes, &sev);
}

int ipc_queue_unregister_notify(mqd_t mqdes) {
    if (mqdes == (mqd_t)-1) {
        errno = EBADF;
        return -1;
    }
    return mq_notify(mqdes, NULL);
}

const char *ipc_queue_strerror(int err_num) {
    switch (err_num) {
        case 0:
            return "Thanh cong (Success)";
        case EEXIST:
            return "Hang doi da ton tai trong he thong (Queue already exists)";
        case ENOENT:
            return "Hang doi khong ton tai (Queue does not exist)";
        case EMSGSIZE:
            return "Kich thuoc thong diep vuot qua gioi han hoac buffer nhan qua nho (Message size exceeds limit or buffer too small)";
        case EAGAIN:
            return "Hang doi da day hoac rong o che do Non-blocking (Queue full/empty in non-blocking mode)";
        case ETIMEDOUT:
            return "Het thoi gian cho xu ly (Operation timed out / timeout)";
        case EACCES:
            return "Khong co quyen truy cap hang doi (Permission denied)";
        case EINTR:
            return "Thao tac bi ngat boi tin hieu he thong (Interrupted by signal)";
        case EBADF:
            return "Queue descriptor khong hop le (Invalid queue descriptor)";
        case EINVAL:
            return "Tham so khong hop le hoac sai dinh dang ten queue (Invalid argument or queue name)";
        case ENFILE:
        case EMFILE:
            return "Da dat gioi han so luong queue mo cua he thong/tien trinh (System or process queue limit reached)";
        case ENOSPC:
            return "Khong du bo nho cho hang doi moi (No space left for new queue)";
        case ENAMETOOLONG:
            return "Ten hang doi qua dai (Queue name too long)";
        default:
            return strerror(err_num);
    }
}
