#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/select.h>
#include <poll.h>
#include <sys/epoll.h>

/* structure that represents a connected client */
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

int send_file(int fd, const char *filename) {

    FILE *file = fopen(filename, "rb");
    if (!file)
        return -1;

    /* move to end to get file size */
    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    rewind(file);

    /* send file size first */
    if (send_all(fd, &filesize, sizeof(filesize)) < 0) {
        fclose(file);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    size_t n;

    /* send file content in chunks */
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send_all(fd, buffer, n) < 0) {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}

void run_server_select(void) {

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

    /* master set contains all active fds */
    fd_set master_set;

    printf("server listening...\n");

    while (1) {

        /* prepare fd set */

        FD_ZERO(&master_set);

        /* always monitor the listening socket */
        FD_SET(server_fd, &master_set);

        /* track the highest fd (required by select) */
        int max_fd = server_fd;

        /* add all connected clients to fd set */
        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i].fd, &master_set);

            /* update max_fd if needed */
            if (clients[i].fd > max_fd)
                max_fd = clients[i].fd;
        }

        /* select modifies fd_set, so we use a copy */
        fd_set read_fds = master_set;

        /* wait for activity */
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        /* -------- new connection -------- */

        if (FD_ISSET(server_fd, &read_fds)) {

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
            send_file(cfd, "chat.log");

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

            /* check if this client's fd is ready */
            if (!FD_ISSET(clients[i].fd, &read_fds))
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

void run_server_poll(void) {
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
            send_file(cfd, "chat.log");

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

void run_server_epoll(void) {

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

    /* create epoll instance */
    int epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); exit(1); }

    /* event structure used for registration */
    struct epoll_event ev;

    /* register listening socket in epoll */
    ev.events = EPOLLIN;          /* we care about read events */
    ev.data.fd = server_fd;       /* store fd inside event */

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl: server_fd");
        exit(1);
    }

    /* array that will receive ready events */
    struct epoll_event events[MAX_CLIENTS + 1];

    printf("server listening...\n");

    while (1) {

        /* wait for events */
        int nfds = epoll_wait(epfd, events, MAX_CLIENTS + 1, -1);
        if (nfds < 0) {
            perror("epoll_wait");
            continue;
        }

        /* iterate over triggered events */
        for (int e = 0; e < nfds; e++) {

            int current_fd = events[e].data.fd;

            /* -------- new connection -------- */

            if (current_fd == server_fd) {

                /* do not exceed max clients */
                if (client_count >= MAX_CLIENTS)
                    continue;

                int cfd = accept(server_fd, NULL, NULL);
                if (cfd < 0)
                    continue;

                /* register new client socket in epoll */
                ev.events = EPOLLIN;
                ev.data.fd = cfd;

                if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
                    perror("epoll_ctl: client_fd");
                    close(cfd);
                    continue;
                }

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
                send_file(cfd, "chat.log");

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
            /* -------- client activity -------- */
            else {

                /* find client index by fd */
                int i;
                for (i = 0; i < client_count; i++) {
                    if (clients[i].fd == current_fd)
                        break;
                }

                if (i == client_count)
                    continue;

                char buf[BUFFER_SIZE];

                /* read incoming data */
                ssize_t n = recv(current_fd, buf, sizeof(buf) - 1, 0);

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

                    /* remove from epoll */
                    epoll_ctl(epfd, EPOLL_CTL_DEL, current_fd, NULL);

                    /* close socket */
                    close(current_fd);

                    /* keep array compact */
                    clients[i] = clients[client_count - 1];
                    client_count--;
                    continue;
                }

                /* ----- normal chat message ----- */

                buf[n] = '\0';

                char timestamp[32];
                make_timestamp(timestamp, sizeof(timestamp));

                char formatted[BUFFER_SIZE + 128];
                snprintf(formatted, sizeof(formatted),
                        "%s:%s → %s\n",
                        timestamp,
                        clients[i].info.name,
                        buf);

                printf("%s", formatted);
                fprintf(logfile, "%s", formatted);
                fflush(logfile);

                broadcast(formatted, strlen(formatted));
            }
        }
    }
}

int main(void) {
    run_server_select();
    // run_server_poll();
    // run_server_epoll();

    return 0;
}