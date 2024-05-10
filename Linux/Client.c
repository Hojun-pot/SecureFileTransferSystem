#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

void trim(char *str) {
    char *ptr = str;
    int len = strlen(ptr);

    while (len > 0 && isspace(ptr[0])) {
        ptr++;
        len--;
    }

    while (len > 0 && isspace(ptr[len - 1])) {
        ptr[--len] = 0;
    }

    memmove(str, ptr, len + 1);
}

int main() {
    int sock;
    struct sockaddr_in server;
    char message[BUFFER_SIZE], server_reply[BUFFER_SIZE];
    int result;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // Existing error handling
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
    if (errno == ECONNREFUSED) {
        fprintf(stderr, "Connection failed: Server is not accepting requests. Check if the server is running.\n");
    } else if (errno == ETIMEDOUT) {
        fprintf(stderr, "Connection failed: The connection attempt timed out. Check your network connectivity.\n");
    } else {
        perror("Connection failed for an unknown reason");
    }
    return 1;
}

    printf("Connected to server.\n");

    while (1) {
        printf("Enter user ID:\n> ");
        fgets(message, sizeof(message), stdin);
        trim(message);

        send(sock, message, strlen(message), 0);

        result = recv(sock, server_reply, BUFFER_SIZE, 0);
        if (result > 0) {
            server_reply[result] = '\0';
            printf("Server reply: %s\n", server_reply);

            if (strstr(server_reply, "Invalid user ID. Enter again: ") != NULL) {
                printf("Invalid ID. Please try again.\n");
                continue;
            } else {
                break;
            }
        } else {
            printf("Failed to receive server response or connection lost.\n");
            close(sock);
            return -1;
        }
    }

    // File path entry loop
    while (1) {
        printf("Enter file path (.txt):\n> ");
        fgets(message, sizeof(message), stdin);
        trim(message);
        send(sock, message, strlen(message), 0);

        result = recv(sock, server_reply, BUFFER_SIZE, 0);
        if (result > 0) {
            server_reply[result] = '\0';
            printf("Server reply: %s\n", server_reply);

            if (strstr(server_reply, "Invalid file extension") != NULL) {
                printf("Invalid file path. Please enter a .txt file path.\n");
                continue;
            } else {
                break;
            }
        } else {
            printf("Failed to receive server response or connection lost.\n");
            close(sock);
            return -1;
        }
    }

    printf("Enter content:\n> ");
    fgets(message, sizeof(message), stdin);
    trim(message);
    send(sock, message, strlen(message), 0);

    result = recv(sock, server_reply, BUFFER_SIZE, 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);
    } else {
        printf("Failed to receive server response.\n");
    }

    close(sock);
    return 0;
}
