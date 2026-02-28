#include "helpers.h"
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

/* send exactly len bytes to the socket.
   this handles partial sends, which are normal for tcp.
   returns total bytes sent or -1 on error. */
ssize_t send_all(int fd, const void *buf, size_t len) {

    size_t total = 0;          /* how many bytes were sent so far */
    const char *p = buf;       /* pointer to current position in buffer */

    while (total < len) {

        /* try to send remaining bytes */
        ssize_t s = send(fd, p + total, len - total, MSG_NOSIGNAL);

        if (s <= 0) {
            /* interrupted by signal → retry */
            if (errno == EINTR)
                continue;

            /* any other error → fail */
            return -1;
        }

        total += s;
    }

    return total;
}

/* receive exactly len bytes from the socket.
   used for fixed-size structs (like Client).
   returns total bytes read or -1 on error. */
ssize_t recv_all(int fd, void *buf, size_t len) {

    size_t total = 0;      /* how many bytes were read */
    char *p = buf;         /* pointer to current write position */

    while (total < len) {

        ssize_t r = recv(fd, p + total, len - total, 0);

        if (r <= 0) {
            /* interrupted by signal → retry */
            if (errno == EINTR)
                continue;

            /* connection closed or error */
            return -1;
        }

        total += r;
    }

    return total;
}

/* create a simple timestamp like: [18:42]
   used for log formatting */
void make_timestamp(char *out, size_t size) {

    time_t now = time(NULL);        /* get current time */
    struct tm *t = localtime(&now); /* convert to local time */

    /* format into [HH:MM] */
    strftime(out, size, "[%H:%M%p]", t);
}