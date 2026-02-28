#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

int receive_file(int fd) {

    long filesize;

    /* receive file size first */
    if (recv_all(fd, &filesize, sizeof(filesize)) < 0)
        return -1;

    long received = 0;
    char buffer[BUFFER_SIZE];

    while (received < filesize) {

        ssize_t n = recv(fd, buffer,
                         (filesize - received > BUFFER_SIZE)
                         ? BUFFER_SIZE
                         : filesize - received,
                         0);

        if (n <= 0)
            return -1;

        received += n;

        /* if this is the last chunk */
        if (received == filesize) {

            /* if file ends with newline, remove only that one */
            if (n > 0 && buffer[n - 1] == '\n') {
                fwrite(buffer, 1, n - 1, stdout);
                continue;
            }
        }

        /* otherwise print everything */
        fwrite(buffer, 1, n, stdout);
    }

    return 0;
}

int main(void) {

    /* create tcp socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    /* configure server address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    /* connect to server */
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); exit(1);
    }

    /* prepare client struct that will be sent to server */
    Client me;
    memset(&me, 0, sizeof(me));

    /* ask user for name */
    printf("Name: ");
    fgets(me.name, MAX_NAME, stdin);

    /* remove trailing newline */
    me.name[strcspn(me.name, "\n")] = 0;

    /* store server ip (for now we just reuse SERVER_IP) */
    strcpy(me.ip, SERVER_IP);

    /* send client metadata to server */
    send_all(sock, &me, sizeof(me));

    /* poll two descriptors:
       0  -> stdin
       sock -> server socket */
    struct pollfd fds[2];
    fds[0].fd = 0;      /* stdin */
    fds[0].events = POLLIN;
    fds[1].fd = sock;   /* socket */
    fds[1].events = POLLIN;

    printf("Connected. Start chatting.\n\n");
    receive_file(sock);
    printf("\nYou: ");
    fflush(stdout);

    while (1) {

        /* wait for input from either stdin or socket */
        if (poll(fds, 2, -1) < 0)
            break;

        /* ---------- user input ---------- */
        if (fds[0].revents & POLLIN) {

            char buf[BUFFER_SIZE];

            /* read line from stdin */
            if (!fgets(buf, sizeof(buf), stdin))
                break;

            /* strip newline */
            buf[strcspn(buf, "\n")] = 0;

            /* ignore empty messages */
            if (strlen(buf) == 0) {
                printf("You: ");
                fflush(stdout);
                continue;
            }

            /* move cursor one line up and clear the old prompt line
               this removes "You: <message>" */
            printf("\033[A\r\033[2K");
            fflush(stdout);

            /* send raw message to server
               server will format and broadcast it back */
            if (send_all(sock, buf, strlen(buf)) < 0)
                break;
        }

        /* ---------- incoming message from server ---------- */
        if (fds[1].revents & POLLIN) {

            char buf[BUFFER_SIZE];

            /* read data from server */
            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0)
                break;

            buf[n] = '\0';

            /* clear current prompt line before printing message */
            printf("\r\033[2K");

            /* print formatted message (already includes timestamp and name) */
            printf("%s", buf);

            /* draw new prompt at the bottom */
            printf("You: ");
            fflush(stdout);
        }
    }

    close(sock);
    return 0;
}