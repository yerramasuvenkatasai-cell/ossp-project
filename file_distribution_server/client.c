/*
 * Client for the High-Concurrency File Distribution Server.
 *
 * Usage:
 *   ./client <server-ip> <port> LIST
 *   ./client <server-ip> <port> GET <filename> [save-as]
 *
 * Talks the same text protocol the server expects:
 *   "GET <name>\n"  or  "LIST\n"   ->   "OK <size>\n"+bytes  or  "ERR msg\n"
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define CHUNK 65536

static int connect_to(const char *ip, int port)
{
    struct sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "bad IP address: %s\n", ip);
        exit(1);
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect");
        exit(1);
    }
    return fd;
}

static int send_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t k = send(fd, p, n, 0);
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        p += k; n -= (size_t)k;
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
        if (k < 0) { if (errno == EINTR) continue; return -1; }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

static int recv_exact(int fd, FILE *out, size_t total)
{
    char buf[CHUNK];
    size_t got = 0;
    while (got < total) {
        size_t want = total - got;
        if (want > sizeof buf) want = sizeof buf;
        ssize_t k = recv(fd, buf, want, 0);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return -1; }
        if (out) fwrite(buf, 1, (size_t)k, out);
        got += (size_t)k;
    }
    return 0;
}

static void do_list(int fd)
{
    char hdr[64];
    int n = read_line(fd, hdr, sizeof hdr);
    long size;
    if (n <= 0) { fprintf(stderr, "no response from server\n"); return; }
    if (sscanf(hdr, "OK %ld", &size) == 1) {
        printf("Files available on server:\n");
        recv_exact(fd, stdout, (size_t)size);
    } else {
        printf("Server said: %s\n", hdr);
    }
}

static void do_get(int fd, const char *remote, const char *save_as)
{
    char cmd[300], hdr[64];
    long size;
    struct timespec t0, t1;
    FILE *out;

    snprintf(cmd, sizeof cmd, "GET %s\n", remote);
    if (send_all(fd, cmd, strlen(cmd)) != 0) { perror("send"); exit(1); }

    if (read_line(fd, hdr, sizeof hdr) <= 0) {
        fprintf(stderr, "no response from server\n");
        exit(1);
    }
    if (sscanf(hdr, "OK %ld", &size) != 1) {
        printf("Server error: %s\n", hdr);
        exit(1);
    }

    out = fopen(save_as, "wb");
    if (!out) { perror("fopen"); exit(1); }

    printf("Downloading %s (%ld bytes) -> %s\n", remote, size, save_as);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (recv_exact(fd, out, (size_t)size) != 0) {
        fprintf(stderr, "download interrupted\n");
        fclose(out);
        exit(1);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fclose(out);

    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    double mbps = secs > 0 ? (size / (1024.0 * 1024.0)) / secs : 0.0;
    printf("Done: %ld bytes in %.3f s (%.2f MB/s)\n", size, secs, mbps);
}

int main(int argc, char **argv)
{
    int fd, port;

    if (argc < 4) {
        fprintf(stderr,
            "usage:\n"
            "  %s <server-ip> <port> LIST\n"
            "  %s <server-ip> <port> GET <filename> [save-as]\n",
            argv[0], argv[0]);
        return 1;
    }

    port = atoi(argv[2]);
    fd = connect_to(argv[1], port);

    if (strcmp(argv[3], "LIST") == 0) {
        send_all(fd, "LIST\n", 5);
        do_list(fd);
    } else if (strcmp(argv[3], "GET") == 0) {
        if (argc < 5) { fprintf(stderr, "GET needs a filename\n"); return 1; }
        const char *save_as = (argc > 5) ? argv[5] : argv[4];
        do_get(fd, argv[4], save_as);
    } else {
        fprintf(stderr, "unknown command: %s\n", argv[3]);
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
