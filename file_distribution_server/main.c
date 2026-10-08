/*
 * High-Concurrency File Distribution Server
 * OS & Systems Programming (25CS2104E) - Section 5, Team 8
 *
 * Flow:  ACCEPT -> QUEUE -> WORKER -> mmap -> SEND -> LOG
 *
 * Modules (same numbering as the PPT):
 *   M1 Network Acceptor   acceptor_main()
 *   M2 Request Queue      queue_push()/queue_pop()   (mutex + 2 semaphores)
 *   M3 Worker Pool        worker_main()
 *   M4 File Manager       file_acquire()/file_release() (ref-counted mmap)
 *   M5 Transfer Engine    handle_get()
 *   M6 Logging            log_activity()/log_download()
 *   M7 Monitor            monitor_main()/print_stats()
 *   M8 Signal Control     on_signal() + sigaction() in main()
 *
 * Protocol (text header, then raw bytes):
 *   client -> "GET <filename>\n"   or   "LIST\n"
 *   server -> "OK <size>\n" + <size> bytes    or    "ERR <message>\n"
 *
 * Build : make          Run : ./server [port] [workers]   (built from main.c)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_PORT     8080
#define DEFAULT_WORKERS  8
#define QUEUE_SIZE       64          /* bounded buffer capacity           */
#define MAX_FILES        128         /* max files mapped at the same time */
#define MAX_NAME         255
#define CHUNK            (64 * 1024) /* bytes per send() call             */
#define SHARE_DIR        "shared"    /* directory that holds shared files */

/* ---------------- M8: signal flags (handler only sets flags) ---------- */
static volatile sig_atomic_t g_stop = 0;   /* SIGINT / SIGTERM */
static volatile sig_atomic_t g_dump = 0;   /* SIGUSR1          */

static void on_signal(int sig)
{
    if (sig == SIGUSR1) g_dump = 1;
    else                g_stop = 1;
}

/* ---------------- statistics (lock-free atomics) ---------------------- */
static _Atomic long st_active, st_total_conn, st_downloads, st_failed, st_bytes;

/* ---------------- M6: logging ----------------------------------------- */
static FILE *f_dl, *f_act;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static void stamp(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%d %H:%M:%S", &tm);
}

static void log_activity(const char *fmt, ...)
{
    char ts[32];
    va_list ap;
    stamp(ts, sizeof ts);
    pthread_mutex_lock(&log_mutex);          /* one lock: lines never mix */
    fprintf(f_act, "[%s] ", ts);
    va_start(ap, fmt);
    vfprintf(f_act, fmt, ap);
    va_end(ap);
    fputc('\n', f_act);
    fflush(f_act);
    pthread_mutex_unlock(&log_mutex);
}

static void log_download(const char *ip, const char *file, long bytes,
                         double secs, const char *status)
{
    char ts[32];
    stamp(ts, sizeof ts);
    pthread_mutex_lock(&log_mutex);
    fprintf(f_dl, "%s | %s | %s | %ld bytes | %.3f s | %s\n",
            ts, ip, file, bytes, secs, status);
    fflush(f_dl);
    pthread_mutex_unlock(&log_mutex);
}

/* ---------------- M2: bounded request queue --------------------------- */
typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
} request_t;

static request_t queue_buf[QUEUE_SIZE];
static int q_head, q_tail, q_count;
static pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t sem_items;   /* counts filled slots (consumers wait on it) */
static sem_t sem_slots;   /* counts free slots   (producer waits on it) */

/* wait on a semaphore for at most 1 s so threads can notice g_stop */
static int sem_wait_1s(sem_t *s)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR) continue;
        return -1;                            /* timeout */
    }
    return 0;
}

static int queue_push(const request_t *r)     /* producer: acceptor */
{
    while (sem_wait_1s(&sem_slots) != 0)
        if (g_stop) return -1;
    pthread_mutex_lock(&q_mutex);
    queue_buf[q_tail] = *r;
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    q_count++;
    pthread_mutex_unlock(&q_mutex);
    sem_post(&sem_items);
    return 0;
}

static int queue_pop(request_t *r)            /* consumer: workers */
{
    while (sem_wait_1s(&sem_items) != 0)
        if (g_stop) return -1;                /* queue empty and stopping */
    pthread_mutex_lock(&q_mutex);
    *r = queue_buf[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count--;
    pthread_mutex_unlock(&q_mutex);
    sem_post(&sem_slots);
    return 0;
}

/* ---------------- M4: file table (one mmap shared by many clients) ---- */
typedef struct {
    int    used;
    char   name[MAX_NAME + 1];
    void  *addr;
    size_t size;
    int    refs;
} fentry_t;

static fentry_t ftab[MAX_FILES];
static pthread_mutex_t ft_mutex = PTHREAD_MUTEX_INITIALIZER;

/* returns 0 ok, -1 not found, -2 table full, -3 mmap failed */
static int file_acquire(const char *name, void **addr, size_t *size)
{
    char path[MAX_NAME + 16];
    struct stat st;
    int i, free_slot = -1, fd;
    void *p = NULL;

    pthread_mutex_lock(&ft_mutex);
    for (i = 0; i < MAX_FILES; i++) {
        if (ftab[i].used && strcmp(ftab[i].name, name) == 0) {
            ftab[i].refs++;                   /* already mapped: share it */
            *addr = ftab[i].addr;
            *size = ftab[i].size;
            pthread_mutex_unlock(&ft_mutex);
            return 0;
        }
        if (!ftab[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) { pthread_mutex_unlock(&ft_mutex); return -2; }

    snprintf(path, sizeof path, "%s/%s", SHARE_DIR, name);
    fd = open(path, O_RDONLY);
    if (fd < 0) { pthread_mutex_unlock(&ft_mutex); return -1; }
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        pthread_mutex_unlock(&ft_mutex);
        return -1;
    }
    if (st.st_size > 0) {
        p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) {
            close(fd);
            pthread_mutex_unlock(&ft_mutex);
            return -3;
        }
        madvise(p, st.st_size, MADV_SEQUENTIAL);
    }
    close(fd);                                /* mapping stays valid */

    ftab[free_slot].used = 1;
    snprintf(ftab[free_slot].name, sizeof ftab[free_slot].name, "%s", name);
    ftab[free_slot].addr = p;
    ftab[free_slot].size = st.st_size;
    ftab[free_slot].refs = 1;
    *addr = p;
    *size = st.st_size;
    pthread_mutex_unlock(&ft_mutex);
    return 0;
}

static void file_release(const char *name)
{
    int i;
    pthread_mutex_lock(&ft_mutex);
    for (i = 0; i < MAX_FILES; i++) {
        if (ftab[i].used && strcmp(ftab[i].name, name) == 0) {
            if (--ftab[i].refs == 0) {        /* last user unmaps */
                if (ftab[i].addr) munmap(ftab[i].addr, ftab[i].size);
                ftab[i].used = 0;
                ftab[i].addr = NULL;
            }
            break;
        }
    }
    pthread_mutex_unlock(&ft_mutex);
}

/* ---------------- helpers --------------------------------------------- */
static int send_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);   /* no SIGPIPE */
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;                        /* client gone or timeout */
        }
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int read_line(int fd, char *buf, size_t n)
{
    size_t i = 0;
    while (i < n - 1) {
        char c;
        ssize_t k = recv(fd, &c, 1, 0);
        if (k == 0) break;
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    while (i > 0 && buf[i - 1] == '\r') buf[--i] = '\0';
    return (int)i;
}

/* reject path tricks: no '/', no "..", no hidden files */
static int valid_name(const char *s)
{
    size_t len = strlen(s);
    if (len == 0 || len > MAX_NAME) return 0;
    if (s[0] == '.' || strchr(s, '/') || strstr(s, "..")) return 0;
    return 1;
}

static void send_err(int fd, const char *msg)
{
    char b[128];
    snprintf(b, sizeof b, "ERR %s\n", msg);
    send_all(fd, b, strlen(b));
}

/* ---------------- M5: transfer engine --------------------------------- */
static void handle_get(int fd, const char *ip, const char *name)
{
    struct timespec t0, t1;
    void *addr = NULL;
    size_t size = 0, off = 0;
    char hdr[64];
    int rc, ok;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    if (!valid_name(name)) {
        send_err(fd, "invalid file name");
        log_download(ip, name, 0, 0, "REJECTED");
        atomic_fetch_add(&st_failed, 1);
        return;
    }
    rc = file_acquire(name, &addr, &size);
    if (rc != 0) {
        send_err(fd, rc == -1 ? "file not found" :
                     rc == -2 ? "server busy" : "cannot map file");
        log_download(ip, name, 0, 0, rc == -1 ? "NOT_FOUND" : "ERROR");
        atomic_fetch_add(&st_failed, 1);
        return;
    }

    snprintf(hdr, sizeof hdr, "OK %zu\n", size);
    ok = (send_all(fd, hdr, strlen(hdr)) == 0);
    while (ok && off < size) {                /* send straight from mapping */
        size_t n = size - off;
        if (n > CHUNK) n = CHUNK;
        if (send_all(fd, (char *)addr + off, n) != 0) { ok = 0; break; }
        off += n;
        atomic_fetch_add(&st_bytes, (long)n);
    }
    file_release(name);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    log_download(ip, name, (long)off, secs, ok ? "OK" : "FAILED");
    atomic_fetch_add(ok ? &st_downloads : &st_failed, 1);
}

static void handle_list(int fd)
{
    char out[8192], hdr[64], path[MAX_NAME + 16];
    size_t len = 0;
    struct dirent *e;
    struct stat st;
    DIR *d = opendir(SHARE_DIR);

    if (!d) { send_err(fd, "cannot open shared directory"); return; }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", SHARE_DIR, e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            int k = snprintf(out + len, sizeof out - len, "%s %lld\n",
                             e->d_name, (long long)st.st_size);
            if (k < 0 || (size_t)k >= sizeof out - len) break;
            len += (size_t)k;
        }
    }
    closedir(d);
    snprintf(hdr, sizeof hdr, "OK %zu\n", len);
    if (send_all(fd, hdr, strlen(hdr)) == 0) send_all(fd, out, len);
}

static void handle_client(const request_t *r)
{
    char line[MAX_NAME + 16];
    int n = read_line(r->fd, line, sizeof line);

    if (n <= 0) { log_activity("EMPTY/BAD REQUEST from %s", r->ip); return; }
    log_activity("REQUEST %s : %s", r->ip, line);

    if (strncmp(line, "GET ", 4) == 0)      handle_get(r->fd, r->ip, line + 4);
    else if (strcmp(line, "LIST") == 0)     handle_list(r->fd);
    else                                    send_err(r->fd, "unknown command");
}

/* ---------------- M3: worker pool ------------------------------------- */
static void *worker_main(void *arg)
{
    request_t r;
    (void)arg;
    while (queue_pop(&r) == 0) {
        handle_client(&r);
        close(r.fd);                          /* never leak a descriptor */
        atomic_fetch_sub(&st_active, 1);
        log_activity("DISCONNECT %s", r.ip);
    }
    return NULL;
}

/* ---------------- M1: network acceptor -------------------------------- */
static int listen_fd = -1;

static void *acceptor_main(void *arg)
{
    struct pollfd p = { .fd = listen_fd, .events = POLLIN };
    (void)arg;

    while (!g_stop) {
        struct sockaddr_in a;
        socklen_t l = sizeof a;
        struct timeval tv;
        request_t r;
        int fd;

        if (poll(&p, 1, 500) <= 0) continue;  /* timeout/EINTR: re-check flag */
        fd = accept(listen_fd, (struct sockaddr *)&a, &l);
        if (fd < 0) continue;

        tv.tv_sec = 30; tv.tv_usec = 0;       /* stuck clients cannot block us */
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        tv.tv_sec = 10;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

        r.fd = fd;
        inet_ntop(AF_INET, &a.sin_addr, r.ip, sizeof r.ip);
        atomic_fetch_add(&st_total_conn, 1);
        atomic_fetch_add(&st_active, 1);
        log_activity("CONNECT %s", r.ip);

        if (queue_push(&r) != 0) {            /* shutting down */
            close(fd);
            atomic_fetch_sub(&st_active, 1);
        }
    }
    return NULL;
}

/* ---------------- M7: monitor thread ---------------------------------- */
static pthread_mutex_t stop_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  stop_cond  = PTHREAD_COND_INITIALIZER;

static long read_vmrss_kb(void)
{
    char line[256];
    long kb = -1;
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb;
}

static void print_stats(void)
{
    struct rusage ru;
    int qlen;
    getrusage(RUSAGE_SELF, &ru);
    pthread_mutex_lock(&q_mutex);
    qlen = q_count;
    pthread_mutex_unlock(&q_mutex);
    printf("[STATS] active=%ld queued=%d connections=%ld downloads=%ld "
           "failed=%ld sent=%.2f MB | VmRSS=%ld KB maxrss=%ld KB "
           "minflt=%ld majflt=%ld\n",
           (long)st_active, qlen, (long)st_total_conn, (long)st_downloads,
           (long)st_failed, (double)st_bytes / (1024.0 * 1024.0),
           read_vmrss_kb(), ru.ru_maxrss, ru.ru_minflt, ru.ru_majflt);
    fflush(stdout);
}

static void *monitor_main(void *arg)
{
    int tick = 0;
    (void)arg;
    while (!g_stop) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_mutex_lock(&stop_mutex);
        pthread_cond_timedwait(&stop_cond, &stop_mutex, &ts);
        pthread_mutex_unlock(&stop_mutex);
        if (g_stop) break;
        tick++;
        if (g_dump)          { g_dump = 0; print_stats(); }   /* kill -USR1 */
        else if (tick % 5 == 0) print_stats();                /* every 5 s  */
    }
    return NULL;
}

/* ---------------- main ------------------------------------------------ */
int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    int nworkers = (argc > 2) ? atoi(argv[2]) : DEFAULT_WORKERS;
    struct sigaction sa;
    struct sockaddr_in addr;
    pthread_t acceptor, monitor, workers[256];
    int i, one = 1;

    if (port < 1 || port > 65535 || nworkers < 1 || nworkers > 256) {
        fprintf(stderr, "usage: %s [port 1-65535] [workers 1-256]\n", argv[0]);
        return 1;
    }

    /* M8: signals. No SA_RESTART so blocking calls return EINTR. */
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = SIG_IGN;                  /* client disconnects */
    sigaction(SIGPIPE, &sa, NULL);

    mkdir(SHARE_DIR, 0755);
    f_dl  = fopen("downloads.log", "a");
    f_act = fopen("activity.log", "a");
    if (!f_dl || !f_act) { perror("fopen log"); return 1; }

    sem_init(&sem_items, 0, 0);               /* no items yet          */
    sem_init(&sem_slots, 0, QUEUE_SIZE);      /* all slots free        */

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); return 1;
    }
    if (listen(listen_fd, 128) < 0) { perror("listen"); return 1; }

    for (i = 0; i < nworkers; i++)
        pthread_create(&workers[i], NULL, worker_main, NULL);
    pthread_create(&monitor,  NULL, monitor_main,  NULL);
    pthread_create(&acceptor, NULL, acceptor_main, NULL);

    printf("Server pid %d listening on port %d with %d workers, sharing ./%s/\n",
           (int)getpid(), port, nworkers, SHARE_DIR);
    printf("Ctrl+C = graceful stop | kill -USR1 %d = stats\n", (int)getpid());
    fflush(stdout);
    log_activity("SERVER START port=%d workers=%d", port, nworkers);

    while (!g_stop) {                         /* main thread just waits */
        struct timespec d = { 0, 200 * 1000 * 1000 };
        nanosleep(&d, NULL);
    }

    printf("\nShutting down: finishing queued transfers...\n");
    pthread_join(acceptor, NULL);
    pthread_mutex_lock(&stop_mutex);
    pthread_cond_broadcast(&stop_cond);       /* wake monitor at once */
    pthread_mutex_unlock(&stop_mutex);
    for (i = 0; i < nworkers; i++) pthread_join(workers[i], NULL);
    pthread_join(monitor, NULL);

    close(listen_fd);
    print_stats();
    log_activity("SERVER STOP");
    fclose(f_dl);
    fclose(f_act);
    sem_destroy(&sem_items);
    sem_destroy(&sem_slots);
    return 0;
}
