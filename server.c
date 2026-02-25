/*
На сервері
    ++ структура клієнта (fd + active)
    ++ список підключених клієнтів
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
#include <errno.h>
#include <time.h>
#include <stdbool.h>

#include "helpers.c"

#include <arpa/inet.h>
#include <asm-generic/socket.h>

Client client_list[NUM_OF_CLIENTS];
int last_client_index = 0;

void handle_client(int client_fd, const char* client_ip, uint16_t client_port) {
    char buffer[BUFFER_SIZE];
    while(1) {
        ssize_t n = recv(client_fd, buffer, BUFFER_SIZE-1, 0);
        if (n <= 0) {
            if ( n < 0 && errno != EINTR) {
                perror("recv");
            }
            break;
        }

        buffer[n] = '\0';
        log_msg(client_ip, client_port, buffer);
        
        printf("You: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send_exactly(client_fd, buffer, strlen(buffer));
    }
}

void add_client(Client client) {
    client_list[last_client_index] = client;
    printf("Client %s added\n", client.name);
}

int main(void)
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    int server_len = sizeof(server_addr);
    int client_len = sizeof(client_addr);



    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    memset(&server_addr, 0, server_len);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr.s_addr);

    
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    
    
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, server_len) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    
    
    
    printf("Server listenint on port %d...\n\n", SERVER_PORT);
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        handle_client(client_fd, client_ip, htons(SERVER_PORT));

        // pid_t pid = fork();
        // if (pid == 0) {             
        //     close(server_fd);       
            
        //     handle_client(client_fd, client_ip, htons(SERVER_PORT));

        //     close(client_fd);
        //     printf("Client disconnected\n");
        //     exit(0);
        // } else if (pid > 0) {       
        //     close(client_fd);       
        //     printf("New client\n");
        // } else {
        //     perror("fork");
        //     close(client_fd);
        //     printf("Client disconnected\n");
        // }
        

        shutdown(client_fd, SHUT_WR);
        close(client_fd);
        printf("Client closed the connection\n");
    }

    close(server_fd);
    return 0;
}