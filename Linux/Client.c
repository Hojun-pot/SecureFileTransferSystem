#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

// 트림 함수
void trim(char *str) {
    char *ptr = str;
    int len = strlen(ptr);

    // Trim leading space
    while(len > 0 && isspace(ptr[0])) {
        ptr++;
        len--;
    }

    // Trim trailing space
    while(len > 0 && isspace(ptr[len-1])) {
        ptr[--len] = 0;
    }

    // Move trimmed string
    memmove(str, ptr, len + 1);
}

int main() {
    int sock;
    struct sockaddr_in server;
    char message[BUFFER_SIZE], server_reply[BUFFER_SIZE];
    int result;

    // 소켓 생성
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // 서버에 연결
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Connected to server.\n");

    // 유저 ID 입력과 서버 응답 처리
    do {
    printf("Enter user ID:\n> ");
    fgets(message, sizeof(message), stdin);
    trim(message); // Trim whitespace

    send(sock, message, strlen(message), 0);

    result = recv(sock, server_reply, BUFFER_SIZE, 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);
    } else {
        printf("Failed to receive server response or connection lost.\n");
        close(sock);
        return -1;
    }
    } while (strstr(server_reply, "Invalid ID") != NULL);
    send(sock, message, strlen(message), 0);

    result = recv(sock, server_reply, BUFFER_SIZE, 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);

        // 파일 경로 입력
        printf("Enter file path:\n> ");
        fgets(message, sizeof(message), stdin);
        trim(message);
        send(sock, message, strlen(message), 0);

        // 파일 내용 입력
        printf("Enter content:\n> ");
        fgets(message, sizeof(message), stdin);
        trim(message);
        send(sock, message, strlen(message), 0);

        // 서버 응답 받기
        result = recv(sock, server_reply, BUFFER_SIZE, 0);
        if (result > 0) {
            server_reply[result] = '\0';
            printf("Server reply: %s\n", server_reply);
        } else {
            printf("Failed to receive server response.\n");
        }
    } else {
        printf("Failed to receive server response or connection lost.\n");
    }

    close(sock);
    return 0;
}
