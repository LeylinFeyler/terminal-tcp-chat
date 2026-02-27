#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

/* structure that represents a connected client:
   - fd: socket descriptor
   - info: basic client metadata (name, ip, etc.) */
typedef struct {
    int fd;
    Client info;
} ConnectedClient;

/* array of active clients */
ConnectedClient clients[MAX_CLIENTS];

/* current number of connected clients */
int client_count = 0;

/* send a message to all connected clients */
void broadcast(const char *msg, size_t len) {
    for (int i = 0; i < client_count; i++) {
        send_all(clients[i].fd, msg, len);
    }
}

int main(void) {

    /* create tcp socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    /* allow quick restart after server crash */
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    /* configure server address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    /* bind socket to address */
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    /* start listening for incoming connections */
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen"); exit(1);
    }

    /* open log file in append mode */
    FILE *logfile = fopen("chat.log", "a");
    if (!logfile) { perror("fopen"); exit(1); }

    /* poll descriptors: 1 for server + N clients */
    struct pollfd fds[MAX_CLIENTS + 1];

    printf("server listening...\n");

    while (1) {

        /* prepare poll array */

        /* first descriptor is always the listening socket */
        fds[0].fd = server_fd;
        fds[0].events = POLLIN;

        /* add all connected clients to poll set */
        for (int i = 0; i < client_count; i++) {
            fds[i + 1].fd = clients[i].fd;
            fds[i + 1].events = POLLIN;
        }

        int nfds = client_count + 1;

        /* wait for activity */
        if (poll(fds, nfds, -1) < 0) {
            perror("poll");
            continue;
        }

        /* -------- new connection -------- */

        if (fds[0].revents & POLLIN) {

            /* do not exceed max clients */
            if (client_count >= MAX_CLIENTS)
                continue;

            int cfd = accept(server_fd, NULL, NULL);
            if (cfd < 0)
                continue;

            /* receive client info struct */
            Client new_client;
            if (recv_all(cfd, &new_client, sizeof(Client)) < 0) {
                close(cfd);
                continue;
            }

            /* store new client */
            clients[client_count].fd = cfd;
            clients[client_count].info = new_client;
            client_count++;

            /* send chat history to the newly connected client */
            FILE *history = fopen("chat.log", "r");
            if (history) {

                char line[BUFFER_SIZE];

                while (fgets(line, sizeof(line), history)) {
                    send_all(cfd, line, strlen(line));
                }

                fclose(history);
            }

            /* create join message */
            char timestamp[32];
            make_timestamp(timestamp, sizeof(timestamp));

            char join_msg[256];
            snprintf(join_msg, sizeof(join_msg),
                    "%s:%s joined the chat\n",
                    timestamp,
                    new_client.name);

            /* print and log join event */
            printf("%s", join_msg);
            fprintf(logfile, "%s", join_msg);
            fflush(logfile);

            /* notify everyone */
            broadcast(join_msg, strlen(join_msg));
        }

        /* -------- messages / disconnect -------- */

        for (int i = 0; i < client_count; i++) {

            /* skip if no data on this client */
            if (!(fds[i + 1].revents & POLLIN))
                continue;

            char buf[BUFFER_SIZE];

            /* read incoming data */
            ssize_t n = recv(clients[i].fd, buf, sizeof(buf) - 1, 0);

            /* ----- client disconnected ----- */
            if (n <= 0) {

                char timestamp[32];
                make_timestamp(timestamp, sizeof(timestamp));

                char leave_msg[256];
                snprintf(leave_msg, sizeof(leave_msg),
                        "%s:%s left the chat\n",
                        timestamp,
                        clients[i].info.name);

                /* print and log leave event */
                printf("%s", leave_msg);
                fprintf(logfile, "%s", leave_msg);
                fflush(logfile);

                /* notify others */
                broadcast(leave_msg, strlen(leave_msg));

                /* close socket and remove client */
                close(clients[i].fd);

                /* swap with last client to keep array compact */
                clients[i] = clients[client_count - 1];
                client_count--;
                i--;  /* re-check this index */
                continue;
            }

            /* ----- normal chat message ----- */

            buf[n] = '\0';  /* null-terminate received data */

            char timestamp[32];
            make_timestamp(timestamp, sizeof(timestamp));

            /* format message with timestamp and name */
            char formatted[BUFFER_SIZE + 128];
            snprintf(formatted, sizeof(formatted),
                    "%s:%s → %s\n",
                    timestamp,
                    clients[i].info.name,
                    buf);

            /* print to server console and log file */
            printf("%s", formatted);
            fprintf(logfile, "%s", formatted);
            fflush(logfile);

            /* broadcast to all clients */
            broadcast(formatted, strlen(formatted));
        }
    }
}