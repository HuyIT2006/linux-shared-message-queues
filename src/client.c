
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "ipc_queue.h"
#include "protocol.h"

#define CLIENT_TIMEOUT_MS 3000

int main(void)
{
    pid_t client_pid = getpid();

    char response_queue_name[IPC_QUEUE_NAME_MAX];
    char request_buffer[PROTO_MAX_MSG_SIZE];
    char response_buffer[PROTO_MAX_MSG_SIZE + 1];

    mqd_t response_mq = (mqd_t)-1;
    mqd_t request_mq = (mqd_t)-1;

    int exit_status = EXIT_FAILURE;

    printf("=================================\n");
    printf("  Shared Message Queue - Client\n");
    printf("=================================\n");

    printf("Client PID: %ld\n", (long)client_pid);

    // Step 1: Build response queue name
    if (ipc_queue_build_response_name(
            response_queue_name,
            sizeof(response_queue_name),
            client_pid) == -1)
    {
        fprintf(stderr, "Failed to build response queue name!\n");
        return EXIT_FAILURE;
    }

    // Step 2: Configure response queue
    QueueConfig config = {
        .max_messages = IPC_DEFAULT_MAX_MSG,
        .max_msg_size = IPC_DEFAULT_MSG_SIZE,
        .is_nonblocking = 0
    };

    // Step 3: Create response queue
    response_mq = ipc_queue_open(
        response_queue_name,
        O_RDONLY | O_CREAT | O_EXCL,
        IPC_QUEUE_DEFAULT_MODE,
        &config
    );

    if (response_mq == (mqd_t)-1)
    {
        fprintf(stderr, "Create response queue failed: %s\n",
                ipc_queue_strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Response queue created: %s\n", response_queue_name);

    // Step 4: Connect to server request queue
    request_mq = ipc_queue_open(
        IPC_REQUEST_QUEUE_NAME,
        O_WRONLY,
        IPC_QUEUE_DEFAULT_MODE,
        NULL
    );

    if (request_mq == (mqd_t)-1)
    {
        fprintf(stderr, "Cannot connect to server: %s\n",
                ipc_queue_strerror(errno));
        goto cleanup;
    }

    printf("Connected to server successfully!\n");

    // Step 5: Build PING request
    int request_len = protocol_build_request_ext(
        request_buffer,
        sizeof(request_buffer),
        client_pid,
        "PING",
        MSG_PRIO_NORMAL,
        ""
    );

    if (request_len < 0)
    {
        fprintf(stderr, "Failed to build PING request!\n");
        goto cleanup;
    }

    printf("Sending request: %s\n", request_buffer);

    // Step 6: Send request to server
    if (ipc_queue_send_timeout(
            request_mq,
            request_buffer,
            (size_t)request_len,
            MSG_PRIO_NORMAL,
            CLIENT_TIMEOUT_MS) == -1)
    {
        fprintf(stderr, "Send request failed: %s\n",
                ipc_queue_strerror(errno));
        goto cleanup;
    }

    printf("Request sent successfully!\n");
    printf("Waiting for server response...\n");

    // Step 7: Receive response with timeout
    ssize_t received = ipc_queue_receive_timeout(
        response_mq,
        response_buffer,
        PROTO_MAX_MSG_SIZE,
        NULL,
        CLIENT_TIMEOUT_MS
    );

    if (received == -1)
    {
        if (errno == ETIMEDOUT)
        {
            fprintf(stderr, "ERROR: Server response timeout (3 seconds)!\n");
        }
        else
        {
            fprintf(stderr, "Receive failed: %s\n",
                    ipc_queue_strerror(errno));
        }

        goto cleanup;
    }

    // Ensure string is null-terminated
    response_buffer[received] = '\0';

    printf("\nServer response: %s\n", response_buffer);

    if (strncmp(response_buffer, "OK|", 3) == 0)
    {
        printf("PING/PONG test PASSED!\n");
        exit_status = EXIT_SUCCESS;
    }
    else
    {
        fprintf(stderr, "Server returned an error or invalid response.\n");
    }

cleanup:
    // Step 8: Close request queue
    if (request_mq != (mqd_t)-1)
    {
        ipc_queue_close(request_mq);
    }

    // Step 9: Close response queue
    if (response_mq != (mqd_t)-1)
    {
        ipc_queue_close(response_mq);
    }

    // Step 10: Remove client's response queue
    if (ipc_queue_unlink(response_queue_name) == -1)
    {
        perror("ipc_queue_unlink");
        exit_status = EXIT_FAILURE;
    }

    printf("Client cleanup completed!\n");

    return exit_status;
}
