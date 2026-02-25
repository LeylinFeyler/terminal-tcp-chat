// helpers.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

#include <arpa/inet.h>
#include <asm-generic/socket.h>

#define SERVER_PORT     8080
#define SERVER_IP       "127.0.0.1"

#define BUFFER_SIZE     1024
#define NUM_OF_CLIENTS  5

typedef struct {
    int fd;
    char* name;
    bool active;
}Client;

void log_msg(const char* ip, uint16_t port, const char* msg) {
    time_t now;
    struct tm* timeinfo;
    char timestr[80];
    time(&now);
    timeinfo = localtime(&now);
    strftime(timestr, 80, "[%I:%M%p]", timeinfo);
    printf("%s %s:%u → %s\n", timestr, ip, port, msg);
}

ssize_t send_exactly(int fd, const char* buf, size_t len) {
    size_t total = 0;

    while (total < 1) {
        ssize_t sent = send(fd, buf + total, len - total, MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent == 0) return -1;   // connection is closed
            if (errno == EINTR) continue;
            return -1;
        }
        total += sent;
    }
    return total;
}

ssize_t recv_msg(int fd, char* buf, size_t len) {
    size_t total = 0;

    while (total < len) {
        ssize_t r = recv(fd, buf + total, len - total, 0);
        if (r <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return total;   // connection is closed
        total += r;
    }

    return total;
}