#ifndef HELPERS_H
#define HELPERS_H

#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

/* default server configuration */
#define SERVER_PORT 8080
#define SERVER_IP   "127.0.0.1"

/* general limits */
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

/* limits for client metadata */
#define MAX_NAME 32
#define MAX_IP   16   /* enough for ipv4 string (xxx.xxx.xxx.xxx\0) */

/* basic client structure exchanged between client and server.
   sent once when client connects. */
typedef struct {
    int32_t id;           /* client id (assigned or used by server) */
    char name[MAX_NAME];  /* username */
    char ip[MAX_IP];      /* ip address in string form */
    uint8_t active;       /* simple flag, reserved for future use */
} Client;

/* send exactly len bytes (handles partial sends) */
ssize_t send_all(int fd, const void *buf, size_t len);

/* receive exactly len bytes (used for fixed-size structs) */
ssize_t recv_all(int fd, void *buf, size_t len);

/* generate timestamp like [HH:MM] */
void make_timestamp(char *out, size_t size);

#endif