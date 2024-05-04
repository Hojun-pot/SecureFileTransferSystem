#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

void trim(char *str) {
    char *ptr = str;
    int len = strlen(ptr);
    while(len > 0 && isspace(ptr[0])) { ptr++; len--; }
    while(len > 0 && isspace(ptr[len-1])) { ptr[--len] = 0; }
    memmove(str, ptr, len + 1);
}

int main() {
    int sock;
    struct sockaddr_in server;
    char message[BUFFER_SIZE], server_reply[BUFFER_SIZE];
    int result;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        return 1;
    }
    printf("Connected to server.\n");

    printf("Enter user ID:\n> ");
    fgets(message, sizeof(message), stdin);
    trim(message);
    send(sock, message, strlen(message), 0);

    result = recv(sock, server_reply, BUFFER_SIZE, 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);
    }

    printf("Enter file path:\n> ");
    fgets(message, sizeof(message), stdin);
    trim(message);
    send(sock, message, strlen(message), 0);

    printf("Enter content:\n> ");
    fgets(message, sizeof(message), stdin);
    send(sock, message, strlen(message), 0);

    result = recv(sock, server_reply, BUFFER_SIZE, 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);
    }
    close(sock);
    return 0;
}
