#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <unistd.h>
#include <string.h>
//#include <sys/socket.h>
//#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024

// Helper function to trim leading and trailing spaces
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
    WSADATA wsaData;
    SOCKET sock;
    struct sockaddr_in server;
    char message[BUFFER_SIZE], server_reply[BUFFER_SIZE];
    int result;

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    sock = socket(AF_INET, SOCK_STREAM, 0);

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        printf("Connection failed: %d\n", WSAGetLastError());
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected to server.\n");

    // 유저 아이디 입력
    while (1) {
        printf("Enter user ID:\n> ");
        fgets(message, sizeof(message), stdin);
        trim(message); // Trim whitespace

        send(sock, message, strlen(message), 0);

        result = recv(sock, server_reply, sizeof(server_reply), 0);
        if (result > 0) {
            server_reply[result] = '\0';
            printf("Server reply: %s\n", server_reply);

            if (strstr(server_reply, "Valid user ID") != NULL) {
                break; // 유효한 유저 아이디일 경우 반복문 종료
            } else {
                printf("Invalid user ID. Please try again.\n");
            }
        } else {
            printf("Failed to receive server response or connection lost.\n");
            break;
        }
    }

    // 파일 경로 입력
    printf("Enter file path:\n> ");
    fgets(message, sizeof(message), stdin);
    trim(message);
    send(sock, message, strlen(message), 0);

    // 파일 내용 입력
    printf("Enter content:\n> ");
    fgets(message, sizeof(message), stdin);
    send(sock, message, strlen(message), 0);

    // 서버 응답 받기
    result = recv(sock, server_reply, sizeof(server_reply), 0);
    if (result > 0) {
        server_reply[result] = '\0';
        printf("Server reply: %s\n", server_reply);
    } else {
        printf("Failed to receive server response or connection lost.\n");
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}