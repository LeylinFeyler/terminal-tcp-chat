#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/select.h>
#include <poll.h>
#include <sys/epoll.h>

/* TODO

command line parameters – server IP/port, name, log file, select/poll/epoll selection.

command interface – the same /quit, /msg, /nick, /help; parse them locally and do not 
search for them via the server.

reconnect/timeout implementation – if the connection is lost, try to reconnect several 
times; timeouts for connect/recv.

input editing and history – interactive input with history (readline can be used).

Encryption/authentication – the client should initiate TLS and transmit login/password.

Visual improvements – colors, notifications, local log (so as not to lose text if the server 
is down).
*/

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

void run_client_select(const char *server_ip) {

    /* create tcp socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    /* configure server address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    //char *server_ip = "192.168.0.103"; 
    inet_pton(AF_INET, server_ip, &addr.sin_addr);

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

    printf("Connected. Start chatting.\n\n");

    /* receive chat history file */
    receive_file(sock);

    printf("\nYou: ");
    fflush(stdout);

    while (1) {

        /* prepare fd_set for select */

        fd_set read_fds;
        FD_ZERO(&read_fds);

        /* monitor stdin */
        FD_SET(STDIN_FILENO, &read_fds);

        /* monitor socket */
        FD_SET(sock, &read_fds);

        /* select requires highest fd + 1 */
        int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        /* wait for input from either stdin or socket */
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
            break;

        /* ---------- user input ---------- */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {

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
        if (FD_ISSET(sock, &read_fds)) {

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
}

void run_client_poll(void) {
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
}

void run_client_epoll(void) {

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

    /* store server ip */
    strcpy(me.ip, SERVER_IP);

    /* send client metadata to server */
    send_all(sock, &me, sizeof(me));

    printf("Connected. Start chatting.\n\n");

    receive_file(sock);

    printf("\nYou: ");
    fflush(stdout);

    /* create epoll instance */
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    struct epoll_event ev;

    /* register stdin */
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
        perror("epoll_ctl stdin"); exit(1);
    }

    /* register socket */
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &ev) < 0) {
        perror("epoll_ctl sock"); exit(1);
    }

    struct epoll_event events[2];

    int running = 1;

    while (running) {

        int nfds = epoll_wait(epfd, events, 2, -1);
        if (nfds < 0)
            break;

        for (int i = 0; i < nfds; i++) {

            int current_fd = events[i].data.fd;

            /* ---------- user input ---------- */
            if (current_fd == STDIN_FILENO) {

                char buf[BUFFER_SIZE];

                if (!fgets(buf, sizeof(buf), stdin)) {
                    running = 0;
                    break;
                }

                buf[strcspn(buf, "\n")] = 0;

                if (strlen(buf) == 0) {
                    printf("You: ");
                    fflush(stdout);
                    continue;
                }

                /* remove old prompt line */
                printf("\033[A\r\033[2K");
                fflush(stdout);

                if (send_all(sock, buf, strlen(buf)) < 0) {
                    running = 0;
                    break;
                }
            }

            /* ---------- incoming message ---------- */
            else if (current_fd == sock) {

                char buf[BUFFER_SIZE];

                ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    running = 0;
                    break;
                }

                buf[n] = '\0';

                /* clear current prompt line */
                printf("\r\033[2K");

                printf("%s", buf);

                printf("You: ");
                fflush(stdout);
            }
        }
    }

    close(sock);
    close(epfd);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    char *server_ip = argv[1];

    run_client_select(server_ip);
    // run_client_poll();
    // run_client_epoll();
    
    return 0;
}