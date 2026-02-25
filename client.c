/*
На сервері
    структура клієнта (fd + active)
    список підключених клієнтів
    логіка додавання клієнта
    логіка видалення клієнта
    прийом повідомлення
    пересилання всім іншим
    неблокуюче очікування (select)

На клієнті
    паралельне читання stdin і socket
    коректний вихід
    обробка закриття сервера
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "helpers.c"

#include <arpa/inet.h>
#include <asm-generic/socket.h>

#define SERVER_PORT 8080
#define SERVER_IP   "127.0.0.1"

#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd;
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, addrlen);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &addr.sin_addr.s_addr) <= 0) {
        fprintf(stderr, "Wrong IP-adress\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (connect(server_fd, (struct sockaddr*)&addr, addrlen) < 0) {
        perror("connect");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected with: %s:%d\n\n", SERVER_IP, SERVER_PORT);

    printf("You: ");
    while (fgets(buffer, BUFFER_SIZE, stdin)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
        
        ssize_t s = send_exactly(server_fd, buffer, len);
        if (s < 0) {
            perror("send");
            break;
        }

        ssize_t n = recv(server_fd, buffer, BUFFER_SIZE, 0); 
        if (n <= 0) {
            if (n < 0) perror("recv");
            printf("Server closed the connection\n");
            break;
        }
        
        buffer[n-1] = '\0';
        log_msg(SERVER_IP, SERVER_PORT, buffer);
        printf("You: "); 
    }

    close(server_fd);
    printf("Connection is closed\n");
    return 0;
}