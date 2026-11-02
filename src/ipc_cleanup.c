/**
 * @file ipc_cleanup.c
 * @brief Standalone System Cleanup and Orphan Queue Inspector Utility
 * @author VanHuy (Thanh vien 1 - IPC Core Engine)
 *
 * Scans Linux /dev/mqueue Virtual Filesystem to detect, inspect, and remove
 * orphaned POSIX Message Queues left behind by crashed processes.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "ipc_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

static void print_banner(void) {
    printf(COLOR_CYAN COLOR_BOLD);
    printf("=================================================================\n");
    printf("      LINUX IPC MESSAGE QUEUE CLEANUP & INSPECTION UTILITY       \n");
    printf("      Author: VanHuy (Thanh vien 1 - IPC Core Engine)            \n");
    printf("=================================================================\n");
    printf(COLOR_RESET);
}

static void print_usage(const char *prog_name) {
    print_banner();
    printf("Su dung: %s [TUY CHON]\n\n", prog_name);
    printf("Cac tuy chon ho tro:\n");
    printf("  -l, --list             Liet ke tat ca cac queue dang ton tai trong /dev/mqueue\n");
    printf("  -a, --all              Xoa sach tat ca queue lien quan den du an (/shared_*)\n");
    printf("  -p, --pid <PID>        Xoa queue response cua client theo PID (/shared_responses_<PID>)\n");
    printf("  -c, --clean <NAME>     Xoa mot queue cu the theo ten (vi du: /shared_requests)\n");
    printf("  -h, --help             Hien thi huong dan su dung nay\n\n");
    printf("Vi du:\n");
    printf("  %s --list\n", prog_name);
    printf("  %s --all\n", prog_name);
    printf("  %s --pid 12345\n", prog_name);
    printf("  %s --clean /shared_requests\n\n", prog_name);
}

static int is_project_queue(const char *name) {
    if (!name) return 0;
    /* Compare names directly or stripped of leading slash */
    const char *clean_name = (name[0] == '/') ? name + 1 : name;
    if (strcmp(clean_name, "shared_requests") == 0) return 1;
    if (strncmp(clean_name, "shared_responses_", 17) == 0) return 1;
    return 0;
}

static int inspect_and_list_queues(void) {
    const char *mqueue_dir = "/dev/mqueue";
    DIR *dir = opendir(mqueue_dir);

    if (!dir) {
        printf(COLOR_YELLOW "[CANH BAO] Khong the mo thu muc %s: %s\n" COLOR_RESET,
               mqueue_dir, strerror(errno));
        printf("Kiem tra queue truc tiep bang POSIX API:\n");

        /* Check primary request queue */
        mqd_t q = mq_open(IPC_REQUEST_QUEUE_NAME, O_RDONLY);
        if (q != (mqd_t)-1) {
            struct mq_attr attr;
            mq_getattr(q, &attr);
            printf("  -> Tim thay %s | Tin nhan ton: %ld/%ld | Kich thuoc toi da: %ld bytes\n",
                   IPC_REQUEST_QUEUE_NAME, attr.mq_curmsgs, attr.mq_maxmsg, attr.mq_msgsize);
            mq_close(q);
        } else {
            printf("  -> %s: Khong ton tai hoac da duoc don dep.\n", IPC_REQUEST_QUEUE_NAME);
        }
        return 0;
    }

    struct dirent *entry;
    int found_total = 0;
    int found_project = 0;

    printf(COLOR_BOLD "Danh sach cac Message Queue trong %s:\n" COLOR_RESET, mqueue_dir);
    printf("%-30s | %-12s | %-16s | %-12s\n", "Ten Queue", "So Tin Hien Co", "Dung Luong Toi Da", "Trang Thai");
    printf("------------------------------------------------------------------------------------\n");

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        found_total++;
        char full_name[512];
        snprintf(full_name, sizeof(full_name), "/%s", entry->d_name);

        int is_proj = is_project_queue(entry->d_name);
        if (is_proj) found_project++;

        /* Open queue to read attributes */
        mqd_t q = mq_open(full_name, O_RDONLY);
        if (q != (mqd_t)-1) {
            struct mq_attr attr;
            if (mq_getattr(q, &attr) == 0) {
                printf("%-30s | %-12ld | %-16ld | %s\n",
                       full_name,
                       attr.mq_curmsgs,
                       attr.mq_maxmsg * attr.mq_msgsize,
                       is_proj ? COLOR_GREEN "DU AN" COLOR_RESET : "He thong khac");
            } else {
                printf("%-30s | %-12s | %-16s | %s\n", full_name, "N/A", "N/A", "Loi doc attr");
            }
            mq_close(q);
        } else {
            printf("%-30s | %-12s | %-16s | %s\n", full_name, "N/A", "N/A", "Permission denied");
        }
    }

    closedir(dir);

    printf("------------------------------------------------------------------------------------\n");
    printf("Tong so queue tim thay: %d (Queue thuoc du an: %s%d%s)\n",
           found_total, COLOR_GREEN, found_project, COLOR_RESET);

    return 0;
}

static int cleanup_single_queue(const char *name) {
    char formatted_name[IPC_QUEUE_NAME_MAX];
    if (name[0] != '/') {
        snprintf(formatted_name, sizeof(formatted_name), "/%s", name);
    } else {
        snprintf(formatted_name, sizeof(formatted_name), "%s", name);
    }

    if (ipc_queue_unlink(formatted_name) == 0) {
        printf(COLOR_GREEN "[THANH CONG] Da xoa queue: %s\n" COLOR_RESET, formatted_name);
        return 0;
    } else {
        if (errno == ENOENT) {
            printf(COLOR_YELLOW "[THONG BAO] Queue %s khong ton tai (da duoc xoa truoc do).\n" COLOR_RESET, formatted_name);
        } else {
            printf(COLOR_RED "[LOI] Khong the xoa queue %s: %s\n" COLOR_RESET,
                   formatted_name, ipc_queue_strerror(errno));
        }
        return -1;
    }
}

static int cleanup_all_project_queues(void) {
    printf(COLOR_CYAN "Dang quet va don dep tat ca cac queue thuoc du an...\n" COLOR_RESET);

    int count = 0;
    /* Clean up the main shared requests queue */
    if (cleanup_single_queue(IPC_REQUEST_QUEUE_NAME) == 0) {
        count++;
    }

    /* Scan /dev/mqueue for client queues */
    DIR *dir = opendir("/dev/mqueue");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (is_project_queue(entry->d_name)) {
                char full_name[512];
                snprintf(full_name, sizeof(full_name), "/%s", entry->d_name);
                if (cleanup_single_queue(full_name) == 0) {
                    count++;
                }
            }
        }
        closedir(dir);
    }

    printf(COLOR_GREEN "Hoan tat don dep! Tong so queue da giai phong: %d\n" COLOR_RESET, count);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    const char *arg = argv[1];

    if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    } else if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list") == 0) {
        print_banner();
        return inspect_and_list_queues();
    } else if (strcmp(arg, "-a") == 0 || strcmp(arg, "--all") == 0) {
        print_banner();
        return cleanup_all_project_queues();
    } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--pid") == 0) {
        if (argc < 3) {
            fprintf(stderr, COLOR_RED "Loi: Thieu tham so PID.\n" COLOR_RESET);
            fprintf(stderr, "Su dung: %s --pid <PID>\n", argv[0]);
            return 1;
        }
        pid_t target_pid = (pid_t)atoi(argv[2]);
        char qname[IPC_QUEUE_NAME_MAX];
        if (ipc_queue_build_response_name(qname, sizeof(qname), target_pid) != 0) {
            fprintf(stderr, COLOR_RED "Loi: PID khong hop le.\n" COLOR_RESET);
            return 1;
        }
        print_banner();
        return cleanup_single_queue(qname);
    } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--clean") == 0) {
        if (argc < 3) {
            fprintf(stderr, COLOR_RED "Loi: Thieu ten queue can xoa.\n" COLOR_RESET);
            fprintf(stderr, "Su dung: %s --clean <NAME>\n", argv[0]);
            return 1;
        }
        print_banner();
        return cleanup_single_queue(argv[2]);
    } else {
        fprintf(stderr, COLOR_RED "Tuy chon khong hop le: %s\n" COLOR_RESET, arg);
        print_usage(argv[0]);
        return 1;
    }
}
