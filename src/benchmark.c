/*
 * benchmark.c
 *
 * Team Member 3 - IPC Client & Benchmark
 *
 * Benchmark for Linux POSIX Shared Message Queues.
 */

#include "ipc_queue.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <stdint.h>
#include <limits.h>

#define BENCH_DEFAULT_CLIENTS 1
#define BENCH_DEFAULT_REQUESTS 10
#define BENCH_TIMEOUT_MS 3000

typedef struct
{
    int clients;
    int requests_per_client;
    const char *command;
    MessagePriority priority;
} BenchmarkConfig;

typedef struct
{
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t skipped_requests;

    double total_latency_ms;
    double min_latency_ms;
    double max_latency_ms;
} BenchmarkStats;

/*
 * IPC connection owned by one benchmark client process.
 */
typedef struct
{
    pid_t pid;
    mqd_t request_mq;
    mqd_t response_mq;
    char response_queue_name[IPC_QUEUE_NAME_MAX];
} BenchmarkClient;

/*
 * Open the shared request queue and create
 * a private response queue for this process.
 */
static int benchmark_client_connect(BenchmarkClient *client)
{
    QueueConfig config = {
        .max_messages = IPC_DEFAULT_MAX_MSG,
        .max_msg_size = IPC_DEFAULT_MSG_SIZE,
        .is_nonblocking = 0
    };

    client->pid = getpid();
    client->request_mq = (mqd_t)-1;
    client->response_mq = (mqd_t)-1;
    client->response_queue_name[0] = '\0';

    if (ipc_queue_build_response_name(
            client->response_queue_name,
            sizeof(client->response_queue_name),
            client->pid) == -1)
    {
        perror("benchmark response queue name");
        return -1;
    }

    client->response_mq = ipc_queue_open(
        client->response_queue_name,
        O_RDONLY | O_CREAT | O_EXCL,
        IPC_QUEUE_DEFAULT_MODE,
        &config
    );

    if (client->response_mq == (mqd_t)-1)
    {
        perror("benchmark response queue");
        return -1;
    }

    client->request_mq = ipc_queue_open(
        IPC_REQUEST_QUEUE_NAME,
        O_WRONLY,
        IPC_QUEUE_DEFAULT_MODE,
        NULL
    );

    if (client->request_mq == (mqd_t)-1)
    {
        perror("benchmark request queue");

        ipc_queue_close(client->response_mq);
        client->response_mq = (mqd_t)-1;

        ipc_queue_unlink(client->response_queue_name);
        client->response_queue_name[0] = '\0';

        return -1;
    }

    return 0;
}

/*
 * Close descriptors and unlink the private response queue.
 */
static void benchmark_client_cleanup(BenchmarkClient *client)
{
    if (client->request_mq != (mqd_t)-1)
    {
        ipc_queue_close(client->request_mq);
        client->request_mq = (mqd_t)-1;
    }

    if (client->response_mq != (mqd_t)-1)
    {
        ipc_queue_close(client->response_mq);
        client->response_mq = (mqd_t)-1;
    }

    if (client->response_queue_name[0] != '\0')
    {
        ipc_queue_unlink(client->response_queue_name);
        client->response_queue_name[0] = '\0';
    }
}

/*
 * Return monotonic time in milliseconds.
 * CLOCK_MONOTONIC is suitable for measuring elapsed time.
 */
static double benchmark_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
    {
        perror("clock_gettime");
        return -1.0;
    }

    return (double)ts.tv_sec * 1000.0 +
           (double)ts.tv_nsec / 1000000.0;
}

/*
 * Execute one benchmark request and measure round-trip latency.
 *
 * Returns:
 *   0  - Successful server response
 *  -1  - Send, receive, or server error
 */
static int benchmark_execute_request(
    BenchmarkClient *client,
    const BenchmarkConfig *config,
    double *latency_ms)
{
    char command[PROTO_MAX_CMD_LEN];
    char request[PROTO_MAX_MSG_SIZE];
    char response[PROTO_MAX_MSG_SIZE + 1];

    const char *space = strchr(config->command, ' ');
    size_t command_len = space
        ? (size_t)(space - config->command)
        : strlen(config->command);

    if (command_len == 0 || command_len >= sizeof(command))
    {
        fprintf(stderr, "Benchmark: invalid command length.\n");
        return -1;
    }

    memcpy(command, config->command, command_len);
    command[command_len] = '\0';

    const char *payload = space ? space + 1 : "";

    if (strlen(payload) >= PROTO_MAX_PAYLOAD_LEN)
    {
        fprintf(stderr, "Benchmark: payload too long.\n");
        return -1;
    }

    int request_len = protocol_build_request_ext(
        request,
        sizeof(request),
        client->pid,
        command,
        config->priority,
        payload
    );

    if (request_len < 0)
    {
        fprintf(stderr, "Benchmark: cannot build request.\n");
        return -1;
    }

    double start_ms = benchmark_now_ms();

    if (start_ms < 0.0)
    {
        return -1;
    }

    if (ipc_queue_send_timeout(
            client->request_mq,
            request,
            (size_t)request_len,
            config->priority,
            BENCH_TIMEOUT_MS) == -1)
    {
        perror("Benchmark send");
        return -1;
    }

    ssize_t received = ipc_queue_receive_timeout(
        client->response_mq,
        response,
        PROTO_MAX_MSG_SIZE,
        NULL,
        BENCH_TIMEOUT_MS
    );

    if (received == -1)
    {
        if (errno == ETIMEDOUT)
        {
            fprintf(stderr, "Benchmark: response timeout.\n");
        }
        else
        {
            perror("Benchmark receive");
        }

        return -1;
    }

    double end_ms = benchmark_now_ms();

    if (end_ms < 0.0)
    {
        return -1;
    }

    response[received] = '\0';
    *latency_ms = end_ms - start_ms;

    if (strncmp(response, "OK|", 3) != 0)
    {
        fprintf(stderr, "Benchmark server error: %s\n", response);
        return -1;
    }

    return 0;
}

/*
 * Run benchmark requests in one client process.
 *
 * Each process owns a unique PID and response queue.
 */
static int benchmark_run_client(
    const BenchmarkConfig *config,
    BenchmarkStats *stats)
{
    BenchmarkClient client = {
        .pid = 0,
        .request_mq = (mqd_t)-1,
        .response_mq = (mqd_t)-1,
        .response_queue_name = ""
    };

    memset(stats, 0, sizeof(*stats));

    if (benchmark_client_connect(&client) == -1)
    {
        fprintf(stderr,
                "Benchmark client %ld: connection failed.\n",
                (long)getpid());

	stats->failed_requests = 0;
	stats->skipped_requests =
		(uint64_t)config->requests_per_client;
        return -1;
    }

    for (int i = 0; i < config->requests_per_client; i++)
    {
        double latency_ms = 0.0;

        int result = benchmark_execute_request(
            &client,
            config,
            &latency_ms
        );

        if (result == 0)
        {
            stats->successful_requests++;
            stats->total_latency_ms += latency_ms;

            if (stats->successful_requests == 1 ||
                latency_ms < stats->min_latency_ms)
            {
                stats->min_latency_ms = latency_ms;
            }

            if (latency_ms > stats->max_latency_ms)
            {
                stats->max_latency_ms = latency_ms;
            }
        }
	else
	{
    	    stats->failed_requests++;

   	    /*
    	     * Stop after a failed request to avoid
             * consuming a delayed response.
             *
             * Remaining requests are skipped,
             * not counted as failed.
            */
   	 stats->skipped_requests +=
       		 (uint64_t)(config->requests_per_client - i - 1);

   	 break;
	}
    }

    benchmark_client_cleanup(&client);

    return stats->failed_requests == 0 ? 0 : -1;
}

/*
 * Merge statistics from one client into total statistics.
 */
static void benchmark_merge_stats(
    BenchmarkStats *total,
    const BenchmarkStats *child)
{
    if (child->successful_requests > 0)
    {
        if (total->successful_requests == 0 ||
            child->min_latency_ms < total->min_latency_ms)
        {
            total->min_latency_ms = child->min_latency_ms;
        }

        if (child->max_latency_ms > total->max_latency_ms)
        {
            total->max_latency_ms = child->max_latency_ms;
        }
    }

    total->successful_requests += child->successful_requests;
    total->failed_requests += child->failed_requests;
    total->skipped_requests += child->skipped_requests;
    total->total_latency_ms += child->total_latency_ms;
}

/*
 * Write all bytes, handling interrupted system calls.
 */
static int benchmark_write_all(
    int fd,
    const void *data,
    size_t size)
{
    const unsigned char *ptr = data;

    while (size > 0)
    {
        ssize_t written = write(fd, ptr, size);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (written == 0)
            return -1;

        ptr += written;
        size -= (size_t)written;
    }

    return 0;
}

/*
 * Read exactly the requested number of bytes.
 */
static int benchmark_read_all(
    int fd,
    void *data,
    size_t size)
{
    unsigned char *ptr = data;

    while (size > 0)
    {
        ssize_t received = read(fd, ptr, size);

        if (received < 0)
        {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (received == 0)
            return -1;

        ptr += received;
        size -= (size_t)received;
    }

    return 0;
}

/*
 * Run multiple benchmark clients concurrently.
 * Each child sends its statistics through a pipe.
 */
static int benchmark_run_parallel(
    const BenchmarkConfig *config,
    BenchmarkStats *total)
{
    int (*pipes)[2] = NULL;
    pid_t *pids = NULL;
    int started = 0;
    int errors = 0;

    memset(total, 0, sizeof(*total));

    pipes = calloc((size_t)config->clients, sizeof(*pipes));
    pids = calloc((size_t)config->clients, sizeof(*pids));

    if (pipes == NULL || pids == NULL)
    {
        perror("Benchmark allocation");
        free(pipes);
        free(pids);
        return -1;
    }

    for (int i = 0; i < config->clients; i++)
    {
        if (pipe(pipes[i]) == -1)
        {
            perror("Benchmark pipe");
            errors = 1;
            break;
        }

        pid_t pid = fork();

        if (pid == -1)
        {
            perror("Benchmark fork");
            close(pipes[i][0]);
            close(pipes[i][1]);
            errors = 1;
            break;
        }

        if (pid == 0)
        {
            BenchmarkStats child_stats;

            close(pipes[i][0]);

            /*
             * Children must not retain pipe descriptors
             * inherited from earlier iterations.
             */
            for (int j = 0; j < i; j++)
            {
                close(pipes[j][0]);
            }

            int result = benchmark_run_client(
                config,
                &child_stats
            );

            int write_result = benchmark_write_all(
                pipes[i][1],
                &child_stats,
                sizeof(child_stats)
            );

            close(pipes[i][1]);

            _exit(
                result == 0 && write_result == 0
                    ? EXIT_SUCCESS
                    : EXIT_FAILURE
            );
        }

        pids[i] = pid;
        close(pipes[i][1]);
        started++;
    }

    for (int i = 0; i < started; i++)
    {
        BenchmarkStats child_stats;

        if (benchmark_read_all(
                pipes[i][0],
                &child_stats,
                sizeof(child_stats)) == 0)
        {
            benchmark_merge_stats(total, &child_stats);
        }
        else
        {
            fprintf(stderr,
                    "Benchmark: cannot read client %d statistics.\n",
                    i + 1);

            total->skipped_requests +=
                (uint64_t)config->requests_per_client;

            errors = 1;
        }

        close(pipes[i][0]);
    }

    for (int i = 0; i < started; i++)
    {
       	 int status = 0;
         pid_t waited;

       	 do
         {
       		 waited = waitpid(pids[i], &status, 0);
   	 }
   	 while (waited == -1 && errno == EINTR);

   	 if (waited == -1)
   	 {
       		 perror("Benchmark waitpid");
       		 errors = 1;
       		 continue;
    	 }

   	 if (!WIFEXITED(status) ||
       		 WEXITSTATUS(status) != EXIT_SUCCESS)
   	 {
       		 errors = 1;
   	 }
}
    total->skipped_requests +=
        (uint64_t)(config->clients - started) *
        (uint64_t)config->requests_per_client;

    free(pipes);
    free(pids);

    return errors == 0 ? 0 : -1;
}

/*
 * Parse a positive integer safely.
 *
 * Returns:
 *   0  - Valid positive integer
 *  -1  - Invalid input
 */
static int parse_positive_int(const char *text, int *result)
{
    char *endptr;
    long value;

    if (text == NULL || text[0] == '\0')
    {
        return -1;
    }

    errno = 0;
    value = strtol(text, &endptr, 10);

    if (errno != 0 ||
        *endptr != '\0' ||
        value <= 0 ||
        value > INT_MAX)
    {
        return -1;
    }

    *result = (int)value;
    return 0;
}

int main(int argc, char *argv[])
{
    BenchmarkConfig config = {
        .clients = BENCH_DEFAULT_CLIENTS,
        .requests_per_client = BENCH_DEFAULT_REQUESTS,
        .command = "PING",
        .priority = MSG_PRIO_NORMAL
    };

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--clients") == 0)
        {
            if (i + 1 >= argc ||
                parse_positive_int(argv[++i], &config.clients) == -1)
            {
                fprintf(stderr, "Invalid --clients value.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--requests") == 0)
        {
            if (i + 1 >= argc ||
                parse_positive_int(
                    argv[++i],
                    &config.requests_per_client) == -1)
            {
                fprintf(stderr, "Invalid --requests value.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--cmd") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Missing --cmd value.\n");
                return EXIT_FAILURE;
            }

            config.command = argv[++i];
        }
        else if (strcmp(argv[i], "--prio") == 0)
        {
            if (i + 1 >= argc ||
                protocol_parse_priority(
                    argv[++i],
                    &config.priority) == -1)
            {
                fprintf(stderr, "Invalid --prio value.\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            printf("Usage:\n");
            printf("  ./bin/benchmark [options]\n\n");
            printf("Options:\n");
            printf("  --clients N     Number of client processes\n");
            printf("  --requests N    Requests per client\n");
            printf("  --cmd COMMAND   Command to execute\n");
            printf("  --prio LEVEL    LOW, NORMAL, HIGH, URGENT\n");
            printf("  --help          Show this help\n");

            return EXIT_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    printf("=================================\n");
    printf(" Shared Message Queue Benchmark\n");
    printf("=================================\n");

    printf("Clients: %d\n", config.clients);
    printf("Requests per client: %d\n",
           config.requests_per_client);
    printf("Command: %s\n", config.command);
    printf("Priority: %s\n",
           protocol_priority_to_str(config.priority));

    printf("\nStarting benchmark...\n");

    BenchmarkStats total_stats;

    double start_ms = benchmark_now_ms();

    if (start_ms < 0.0)
    {
    	return EXIT_FAILURE;
    }

    int result = benchmark_run_parallel(
    	&config,
    	&total_stats
    );

    double end_ms = benchmark_now_ms();

    if (end_ms < 0.0)
    {
    	return EXIT_FAILURE;
    }

    double elapsed_ms = end_ms - start_ms;

    uint64_t completed_requests =
    	total_stats.successful_requests +
    	total_stats.failed_requests;

    double average_latency_ms = 0.0;
    double throughput = 0.0;

    if (total_stats.successful_requests > 0)
    {
    	average_latency_ms =
        	total_stats.total_latency_ms /
        	(double)total_stats.successful_requests;
    }

if (elapsed_ms > 0.0)
{
    throughput =
        (double)completed_requests * 1000.0 /
        elapsed_ms;
}

printf("\n=================================\n");
printf(" Benchmark Results\n");
printf("=================================\n");

printf("Total planned requests: %llu\n",
       (unsigned long long)config.clients *
       (unsigned long long)config.requests_per_client);

printf("Successful requests: %llu\n",
       (unsigned long long)total_stats.successful_requests);

printf("Failed requests: %llu\n",
       (unsigned long long)total_stats.failed_requests);

printf("Skipped requests: %llu\n",
       (unsigned long long)total_stats.skipped_requests);

printf("Elapsed time: %.3f ms\n", elapsed_ms);

printf("Throughput: %.2f requests/sec\n", throughput);

printf("Average latency: %.3f ms\n", average_latency_ms);

printf("Minimum latency: %.3f ms\n",
       total_stats.min_latency_ms);

printf("Maximum latency: %.3f ms\n",
       total_stats.max_latency_ms);

return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    return EXIT_SUCCESS;
}
