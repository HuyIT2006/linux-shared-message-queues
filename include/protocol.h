#ifndef PROTOCOL_H
#define PROTOCOL_H

/**
 * @file protocol.h
 * @brief Communication Protocol, Request Parser & System Services for Linux IPC
 * @author Thanh vien 2 (Server Architecture, Worker Pool & System Services)
 *
 * Implements safe request parsing, validation, response serialization, and
 * business logic handlers for the Linux POSIX Message Queue IPC system.
 *
 * Supports both:
 * 1. Basic 3-field Request Format (CAU_TRUC_YEU_CAU.md):
 *      Request:  <PID>|<COMMAND>|<ARGUMENT>
 *      Response: <STATUS>|<MESSAGE>
 * 2. Extended 4-field Request Format (PHAN_CONG_NHIEM_VU.md):
 *      Request:  <CLIENT_PID>|<COMMAND>|<PRIORITY>|<PAYLOAD>
 *      Response: <STATUS_CODE>|<EXEC_TIME_MS>|<PAYLOAD>
 */

#include "ipc_queue.h"

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Protocol Constants & Buffer Limits
 * ========================================================================= */
#define PROTO_MAX_MSG_SIZE          IPC_DEFAULT_MSG_SIZE    /**< 256 bytes max message */
#define PROTO_MAX_CMD_LEN           32                      /**< Maximum command name length */
#define PROTO_MAX_PAYLOAD_LEN       224                     /**< Maximum payload/argument length */
#define PROTO_DELIMITER             '|'                     /**< Field separator character */
#define PROTO_FIB_MAX_N             45                      /**< Max Fibonacci index (0 <= n <= 45) */

/* Standard Error Payload Strings (as specified in CAU_TRUC_YEU_CAU.md) */
#define PROTO_STR_OK                "OK"
#define PROTO_STR_ERROR             "ERROR"
#define PROTO_ERR_STR_INVALID_REQ   "INVALID_REQUEST"
#define PROTO_ERR_STR_UNKNOWN_CMD   "UNKNOWN_COMMAND"
#define PROTO_ERR_STR_INVALID_ARG   "INVALID_ARGUMENT"
#define PROTO_ERR_STR_OVERFLOW      "BUFFER_OVERFLOW"
#define PROTO_ERR_STR_INTERNAL      "INTERNAL_ERROR"

/* =========================================================================
 * Enumerations
 * ========================================================================= */

/**
 * @brief Supported Server Commands.
 */
typedef enum {
    CMD_UNKNOWN = 0,
    CMD_PING,       /**< Heartbeat check -> PONG */
    CMD_TIME,       /**< Server timestamp (ISO 8601) */
    CMD_FIB,        /**< Fibonacci number calculation (0 <= n <= 45) */
    CMD_UPPER,      /**< Convert string to uppercase */
    CMD_SYSINFO,    /**< Linux /proc system metrics (loadavg, meminfo, uptime) */
    CMD_STATS,      /**< Server runtime statistics */
    CMD_ECHO,       /**< Echo back payload string */
    CMD_CALC        /**< Basic integer arithmetic expression (<a op b>) */
} CommandType;

/**
 * @brief High-level response status.
 */
typedef enum {
    PROTO_STATUS_OK = 0,
    PROTO_STATUS_ERROR = 1
} ProtoStatus;

/**
 * @brief Detailed protocol parsing & execution error codes.
 */
typedef enum {
    PROTO_OK = 0,
    PROTO_ERR_INVALID_REQUEST,      /**< Malformed packet, missing '|', bad PID, etc. */
    PROTO_ERR_UNKNOWN_COMMAND,      /**< Unrecognized command name */
    PROTO_ERR_INVALID_ARGUMENT,     /**< Out-of-range FIB, empty UPPER, unexpected arg */
    PROTO_ERR_BUFFER_OVERFLOW,      /**< Message exceeds 256 bytes or buffer capacity */
    PROTO_ERR_INTERNAL              /**< System or internal execution error */
} ProtoErrorCode;

/**
 * @brief Response serialization mode.
 */
typedef enum {
    PROTO_MODE_AUTO = 0,        /**< Match request format (3-field -> basic, 4-field -> extended) */
    PROTO_MODE_BASIC = 1,       /**< Always <STATUS>|<MESSAGE> */
    PROTO_MODE_EXTENDED = 2     /**< Always <STATUS>|<EXEC_TIME_MS>|<PAYLOAD> */
} ProtoFormatMode;

/* =========================================================================
 * Data Structures
 * ========================================================================= */

/**
 * @brief Parsed and validated client request.
 */
typedef struct {
    pid_t client_pid;                       /**< Client process ID (> 0) */
    CommandType cmd_type;                   /**< Resolved command enumeration */
    char command[PROTO_MAX_CMD_LEN];        /**< Normalized (uppercase) command string */
    MessagePriority priority;               /**< Effective request priority */
    char payload[PROTO_MAX_PAYLOAD_LEN];    /**< Request argument / payload */
    bool is_extended_format;                /**< true if parsed from 4-field request */
} IpcRequest;

/**
 * @brief Structured server response prior to wire serialization.
 */
typedef struct {
    ProtoStatus status;                     /**< PROTO_STATUS_OK or PROTO_STATUS_ERROR */
    ProtoErrorCode error_code;              /**< PROTO_OK or specific error code */
    double exec_time_ms;                    /**< Processing duration in milliseconds */
    char payload[PROTO_MAX_PAYLOAD_LEN];    /**< Result string or error identifier */
} IpcResponse;

/**
 * @brief Read-only snapshot of server operational statistics for STATS command.
 */
typedef struct {
    uint64_t total_requests;                /**< Total requests processed so far */
    uint64_t failed_requests;               /**< Total requests that returned ERROR */
    int active_workers;                     /**< Number of currently busy worker threads */
    int total_workers;                      /**< Total worker threads configured */
    int queued_jobs;                        /**< Requests waiting in internal job queue */
    long uptime_seconds;                    /**< Server uptime in seconds */
} ServerStatsSnapshot;

/* =========================================================================
 * Protocol Parsing & Serialization APIs
 * ========================================================================= */

/**
 * @brief Safely parse and validate a raw request string.
 *
 * Automatically accepts both:
 *   - 3-field format: <PID>|<COMMAND>|<ARGUMENT>
 *   - 4-field format: <CLIENT_PID>|<COMMAND>|<PRIORITY>|<PAYLOAD>
 *
 * Protects against buffer overflows, null-byte injection, pipe injection in
 * arguments, and invalid PIDs.
 *
 * @param raw_msg Pointer to raw null-terminated message received from queue.
 * @param msg_len Length of raw_msg in bytes.
 * @param mq_prio Priority value obtained from mq_receive() (used as default for 3-field).
 * @param out_req Output structure to populate.
 * @return PROTO_OK on valid syntax (note: cmd_type may be CMD_UNKNOWN if command is unrecognized),
 *         or PROTO_ERR_INVALID_REQUEST / PROTO_ERR_BUFFER_OVERFLOW on malformed input.
 */
ProtoErrorCode protocol_parse_request(const char *raw_msg, size_t msg_len,
                                      unsigned int mq_prio, IpcRequest *out_req);

/**
 * @brief Build a 3-field request string: <PID>|<COMMAND>|<ARGUMENT>.
 *
 * @param dest Output buffer.
 * @param dest_size Capacity of dest buffer.
 * @param client_pid Client PID (> 0).
 * @param cmd Command name (e.g., "PING", "FIB").
 * @param arg Optional argument string (can be NULL or "").
 * @return Number of bytes written (excluding null terminator), or -1 on error.
 */
int protocol_build_request(char *dest, size_t dest_size, pid_t client_pid,
                           const char *cmd, const char *arg);

/**
 * @brief Build a 4-field extended request string: <PID>|<COMMAND>|<PRIORITY>|<PAYLOAD>.
 *
 * @param dest Output buffer.
 * @param dest_size Capacity of dest buffer.
 * @param client_pid Client PID (> 0).
 * @param cmd Command name.
 * @param prio Message priority level.
 * @param payload Optional argument string (can be NULL or "").
 * @return Number of bytes written (excluding null terminator), or -1 on error.
 */
int protocol_build_request_ext(char *dest, size_t dest_size, pid_t client_pid,
                               const char *cmd, MessagePriority prio, const char *payload);

/**
 * @brief Serialize an IpcResponse into a wire-ready string buffer.
 *
 * - When include_exec_time == false: formats as `<STATUS>|<PAYLOAD>`
 *   (e.g., `OK|PONG`, `ERROR|UNKNOWN_COMMAND`).
 * - When include_exec_time == true: formats as `<STATUS>|<EXEC_TIME_MS>|<PAYLOAD>`
 *   (e.g., `OK|0.042|PONG`, `ERROR|0.010|UNKNOWN_COMMAND`).
 *
 * @param resp Pointer to IpcResponse structure.
 * @param dest Output buffer (should be >= PROTO_MAX_MSG_SIZE).
 * @param dest_size Size of output buffer.
 * @param include_exec_time Whether to include the execution time field.
 * @return Number of bytes written on success, or -1 on buffer overflow / invalid args.
 */
int protocol_serialize_response(const IpcResponse *resp, char *dest,
                                size_t dest_size, bool include_exec_time);

/* =========================================================================
 * Request Execution & System Service Handlers
 * ========================================================================= */

/**
 * @brief Execute a parsed request and populate the response (including timing).
 *
 * @param req Validated IpcRequest structure.
 * @param out_resp Output IpcResponse structure.
 * @param stats Optional pointer to server runtime stats snapshot (for STATS command).
 * @return PROTO_OK if command succeeded, or the corresponding ProtoErrorCode.
 */
ProtoErrorCode protocol_execute_request(const IpcRequest *req, IpcResponse *out_resp,
                                        const ServerStatsSnapshot *stats);

/**
 * @brief Handle PING service: validates empty arg, writes "PONG".
 */
ProtoErrorCode service_handle_ping(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle TIME service: validates empty arg, writes ISO 8601 timestamp.
 */
ProtoErrorCode service_handle_time(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle FIB service: computes Fibonacci(n) for 0 <= n <= 45.
 */
ProtoErrorCode service_handle_fib(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle UPPER service: converts non-empty argument string to uppercase.
 */
ProtoErrorCode service_handle_upper(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle SYSINFO service: reads Linux /proc/uptime, /proc/loadavg, /proc/meminfo.
 */
ProtoErrorCode service_handle_sysinfo(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle STATS service: formats server runtime metrics.
 */
ProtoErrorCode service_handle_stats(const char *arg, const ServerStatsSnapshot *stats,
                                    char *out_buf, size_t out_size);

/**
 * @brief Handle ECHO service: returns non-empty argument string.
 */
ProtoErrorCode service_handle_echo(const char *arg, char *out_buf, size_t out_size);

/**
 * @brief Handle CALC service: evaluates "<int64> <op> <int64>" (+, -, *, /, %).
 */
ProtoErrorCode service_handle_calc(const char *arg, char *out_buf, size_t out_size);

/* =========================================================================
 * Utility Helpers
 * ========================================================================= */

/**
 * @brief Map command name string (case-insensitive) to CommandType enum.
 */
CommandType protocol_parse_command_type(const char *cmd_str);

/**
 * @brief Parse priority string ("LOW", "NORMAL", "HIGH", "URGENT" or numeric) to MessagePriority.
 * @return 0 on success, -1 on invalid priority string.
 */
int protocol_parse_priority(const char *prio_str, MessagePriority *out_prio);

/**
 * @brief Convert MessagePriority enum to canonical string ("LOW", "NORMAL", "HIGH", "URGENT").
 */
const char *protocol_priority_to_str(MessagePriority prio);

/**
 * @brief Convert ProtoErrorCode to standard protocol error string.
 */
const char *protocol_error_to_str(ProtoErrorCode err);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */