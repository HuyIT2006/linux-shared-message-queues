
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

/*
 * Client connection context.
 * Stores IPC resources for the entire REPL session.
 */
typedef struct
{
    pid_t pid;
    mqd_t request_mq;
    mqd_t response_mq;
    char response_queue_name[IPC_QUEUE_NAME_MAX];
} ClientContext;

static int client_repl(void);

static int client_connect(ClientContext *ctx);

static int client_execute(
    ClientContext *ctx,
    const char *command_input,
    MessagePriority priority
);

static void client_cleanup(ClientContext *ctx);

int main(int argc, char *argv[])
{
     if (argc == 1)
    {
	return client_repl();
    }

    const char *command_input = "PING";
    const char *priority_input = "NORMAL";

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--cmd") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing value for --cmd\n");
                return EXIT_FAILURE;
            }
            command_input = argv[++i];
        }
        else if (strcmp(argv[i], "--prio") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing value for --prio\n");
                return EXIT_FAILURE;
            }
            priority_input = argv[++i];
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    MessagePriority priority;

    if (protocol_parse_priority(priority_input, &priority) != 0)
    {
        fprintf(stderr, "Invalid priority: %s\n", priority_input);
        return EXIT_FAILURE;
    }

    char command[PROTO_MAX_CMD_LEN];
    char payload[PROTO_MAX_PAYLOAD_LEN];

    const char *space = strchr(command_input, ' ');
    size_t command_len = space
        ? (size_t)(space - command_input)
        : strlen(command_input);

    if (command_len == 0 || command_len >= sizeof(command))
    {
        fprintf(stderr, "Invalid command length\n");
        return EXIT_FAILURE;
    }

    memcpy(command, command_input, command_len);
    command[command_len] = '\0';

    const char *argument = space ? space + 1 : "";

    if (strlen(argument) >= sizeof(payload))
    {
        fprintf(stderr, "Payload too long\n");
        return EXIT_FAILURE;
    }

    strcpy(payload, argument);

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
        command,
        priority,
        payload
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
            priority,
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
    printf("Command '%s' executed successfully!\n", command);
    exit_status = EXIT_SUCCESS;
}
else
{
    fprintf(stderr,
            "Command '%s' failed or returned an invalid response.\n",
            command);
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


/*
 * Interactive REPL Shell
 * Team Member 3 - IPC Client
 *
 * Read-Eval-Print Loop:
 * Read user command, execute and print result.
 */
static int client_repl(void)
{
    char input[PROTO_MAX_MSG_SIZE];
    MessagePriority current_priority = MSG_PRIO_NORMAL; 
    ClientContext ctx = {
        .pid = 0,
        .request_mq = (mqd_t)-1,
        .response_mq = (mqd_t)-1,
        .response_queue_name = ""
    };

    if (client_connect(&ctx) == -1)
    {
        fprintf(stderr, "Error: Cannot connect to server.\n");
        return EXIT_FAILURE;
    }

    printf("\n=================================\n");
    printf(" Interactive REPL Mode\n");
    printf("=================================\n");
    printf("Type 'help' for available commands.\n");
    printf("Type 'exit' or 'quit' to leave.\n\n");

    while (1)
    {
        printf("client-ipc> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("\nGoodbye!\n");
            break;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = '\0';

        // Ignore empty input
        if (input[0] == '\0')
        {
            continue;
        }

        // Exit REPL
        if (strcmp(input, "exit") == 0 ||
            strcmp(input, "quit") == 0)
        {
            printf("Goodbye!\n");
            break;
        }

        // Show help menu
        if (strcmp(input, "help") == 0)
        {
            printf("\nAvailable commands:\n");
            printf("  PING          - Check server connection\n");
            printf("  TIME          - Get server time\n");
            printf("  FIB <n>       - Calculate Fibonacci\n");
            printf("  UPPER <text>  - Convert text to uppercase\n");
            printf("  ECHO <text>   - Echo text\n");
            printf("  SYSINFO       - Show system information\n");
            printf("  STATS         - Show server statistics\n");
            printf("  CALC <expr>   - Calculate expression\n");
            printf("  help          - Show this menu\n");
            printf("  exit / quit   - Exit client\n\n");
	    printf("  priority <level> - Set LOW, NORMAL, HIGH or URGENT\n");
            continue;
        }

	
        if (strncmp(input, "priority ", 9) == 0)
        {
            MessagePriority new_priority;

            if (protocol_parse_priority(
                    input + 9, &new_priority) == 0)
            {
                current_priority = new_priority;

                printf("Priority changed to %s (%u)\n",
                       protocol_priority_to_str(current_priority),
                       (unsigned int)current_priority);
            }
            else
            {
                fprintf(stderr,
                        "Invalid priority. Use LOW, NORMAL, HIGH or URGENT.\n");
            }

            continue;
        }

	
	int result = client_execute(
    &ctx,
    input,
    current_priority
);

if (result == -2)
{
    fprintf(stderr,
            "Fatal IPC error. Closing REPL session.\n");

    client_cleanup(&ctx);
    return EXIT_FAILURE;
}

if (result == -1)
{
    fprintf(stderr,
            "Failed to execute command: %s\n",
            input);
}
    }
    client_cleanup(&ctx);
    return EXIT_SUCCESS;
}


/*
 * Initialize IPC connection for the client.
 *
 * Returns:
 *   0  - Success
 *  -1  - Failure
 */
static int client_connect(ClientContext *ctx)
{
    QueueConfig config = {
        .max_messages = IPC_DEFAULT_MAX_MSG,
        .max_msg_size = IPC_DEFAULT_MSG_SIZE,
        .is_nonblocking = 0
    };

    ctx->pid = getpid();
    ctx->request_mq = (mqd_t)-1;
    ctx->response_mq = (mqd_t)-1;

    if (ipc_queue_build_response_name(
            ctx->response_queue_name,
            sizeof(ctx->response_queue_name),
            ctx->pid) == -1)
    {
        perror("ipc_queue_build_response_name");
        return -1;
    }

    ctx->response_mq = ipc_queue_open(
        ctx->response_queue_name,
        O_RDONLY | O_CREAT | O_EXCL,
        IPC_QUEUE_DEFAULT_MODE,
        &config
    );

    if (ctx->response_mq == (mqd_t)-1)
    {
        perror("ipc_queue_open response");
        return -1;
    }

    ctx->request_mq = ipc_queue_open(
        IPC_REQUEST_QUEUE_NAME,
        O_WRONLY,
        IPC_QUEUE_DEFAULT_MODE,
        NULL
    );

    if (ctx->request_mq == (mqd_t)-1)
    {
        perror("ipc_queue_open request");

        ipc_queue_close(ctx->response_mq);
        ctx->response_mq = (mqd_t)-1;

        ipc_queue_unlink(ctx->response_queue_name);

        return -1;
    }

    printf("Connected to server successfully!\n");
    printf("Client PID: %ld\n", (long)ctx->pid);

    return 0;
}


/*
 * Send a command to the server and receive its response.
 *
 * Returns:
 *   0  - Server executed command successfully
 *  -1  - IPC error or server returned an error
 */
static int client_execute(
    ClientContext *ctx,
    const char *command_input,
    MessagePriority priority)
{
    char command[PROTO_MAX_CMD_LEN];
    char payload[PROTO_MAX_PAYLOAD_LEN];
    char request_buffer[PROTO_MAX_MSG_SIZE];
    char response_buffer[PROTO_MAX_MSG_SIZE + 1];

    const char *space = strchr(command_input, ' ');
    size_t command_len = space
        ? (size_t)(space - command_input)
        : strlen(command_input);

    if (command_len == 0 ||
        command_len >= sizeof(command))
    {
        fprintf(stderr, "Error: Invalid command length.\n");
        return -1;
    }

    memcpy(command, command_input, command_len);
    command[command_len] = '\0';

    const char *argument = space ? space + 1 : "";

    if (strlen(argument) >= sizeof(payload))
    {
        fprintf(stderr, "Error: Payload too long.\n");
        return -1;
    }

    strcpy(payload, argument);

    int request_len = protocol_build_request_ext(
        request_buffer,
        sizeof(request_buffer),
        ctx->pid,
        command,
        priority,
        payload
    );

    if (request_len < 0)
    {
        fprintf(stderr, "Error: Cannot build request.\n");
        return -1;
    }

    if (ipc_queue_send_timeout(
            ctx->request_mq,
            request_buffer,
            (size_t)request_len,
            priority,
            CLIENT_TIMEOUT_MS) == -1)
    {
        perror("ipc_queue_send_timeout");
        return -2;
    }

    ssize_t received = ipc_queue_receive_timeout(
        ctx->response_mq,
        response_buffer,
        PROTO_MAX_MSG_SIZE,
        NULL,
        CLIENT_TIMEOUT_MS
    );

    if (received 

== -1)
    {
        if (errno == ETIMEDOUT)
        {
            fprintf(stderr,
                    "Error: Server response timed out.\n");
        }
        else
        {
            perror("ipc_queue_receive_timeout");
        }

        return -2;
    }

    response_buffer[received] = '\0';

    printf("Server response: %s\n", response_buffer);

    if (strncmp(response_buffer, "OK|", 3) == 0)
    {
        return 0;
    }

    fprintf(stderr, "Server returned an error.\n");
    return -1;
}


/*
 * Release all IPC resources owned by the client.
 *
 * Close request/response descriptors and remove
 * the client-specific response queue.
 */
static void client_cleanup(ClientContext *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    if (ctx->request_mq != (mqd_t)-1)
    {
        if (ipc_queue_close(ctx->request_mq) == -1)
        {
            perror("ipc_queue_close request");
        }

        ctx->request_mq = (mqd_t)-1;
    }

    if (ctx->response_mq != (mqd_t)-1)
    {
        if (ipc_queue_close(ctx->response_mq) == -1)
        {
            perror("ipc_queue_close response");
        }

        ctx->response_mq = (mqd_t)-1;
    }

    if (ctx->response_queue_name[0] != '\0')
    {
        if (ipc_queue_unlink(ctx->response_queue_name) == -1)
        {
            perror("ipc_queue_unlink response");
        }

        ctx->response_queue_name[0] = '\0';
    }

    printf("Client cleanup completed!\n");
}
