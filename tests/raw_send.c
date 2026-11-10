/* raw_send.c - cong cu test cua TV4: gui message tho (khong qua protocol parser
 * cua client) vao /shared_requests de kiem tra server co bi crash hay khong.
 *
 * Cach dung:
 *   raw_send [--no-wait] [--hold SEC] [--len N] <message>
 *   Trong <message>, chuoi "{PID}" duoc thay bang PID cua raw_send.
 *   --len N : gui dung N byte (cat bot hoac them ky tu 'A' cho du N, khong co '\0')
 *   --hold S: giu queue response S giay roi moi thoat (de test kill -9 khi dang giu queue)
 * In response (neu co) ra stdout. Exit: 0 nhan duoc response, 3 timeout, 4 gui loi.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MSG_MAX 256

int main(int argc, char *argv[])
{
    int wait_resp = 1, hold = 0, len = -1, i = 1;
    for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
        if (!strcmp(argv[i], "--no-wait")) wait_resp = 0;
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--len") && i + 1 < argc) len = atoi(argv[++i]);
        else break;
    }
    const char *tmpl = (i < argc) ? argv[i] : "";

    char msg[1024], pidstr[16];
    snprintf(pidstr, sizeof pidstr, "%d", (int)getpid());
    msg[0] = '\0';
    for (const char *p = tmpl; *p && strlen(msg) < sizeof msg - 20;) {
        if (!strncmp(p, "{PID}", 5)) { strcat(msg, pidstr); p += 5; }
        else { size_t n = strlen(msg); msg[n] = *p++; msg[n + 1] = '\0'; }
    }
    size_t mlen = strlen(msg);
    if (len >= 0) {                       /* ep do dai chinh xac, khong them '\0' */
        if ((size_t)len > sizeof msg - 1) len = sizeof msg - 1;
        for (size_t k = mlen; k < (size_t)len; k++) msg[k] = 'A';
        mlen = (size_t)len;
    }

    char qname[64];
    snprintf(qname, sizeof qname, "/shared_responses_%d", (int)getpid());
    struct mq_attr at = { .mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = MSG_MAX };
    mqd_t rq = mq_open(qname, O_CREAT | O_EXCL | O_RDONLY, 0660, &at);
    if (rq == (mqd_t)-1) { perror("mq_open response"); return 4; }

    mqd_t sq = mq_open("/shared_requests", O_WRONLY);
    if (sq == (mqd_t)-1) { perror("mq_open request"); mq_close(rq); mq_unlink(qname); return 4; }

    int rc = 0;
    if (mq_send(sq, msg, mlen, 10) == -1) {
        printf("SEND_ERROR|%s\n", strerror(errno));
        rc = 4;
    } else if (wait_resp) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 3;
        char buf[MSG_MAX + 1];
        ssize_t n = mq_timedreceive(rq, buf, MSG_MAX, NULL, &ts);
        if (n >= 0) { buf[n] = '\0'; printf("%s\n", buf); }
        else { printf("TIMEOUT\n"); rc = 3; }
    }
    if (hold > 0) sleep((unsigned)hold);
    mq_close(sq); mq_close(rq); mq_unlink(qname);
    return rc;
}
