#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <pthread.h>
#include <time.h>
#include <ctype.h>  // for isspace
#include <arpa/inet.h>  // for inet_ntoa

#define PORT 8080
#define MAX_CONN 10
#define BUFFER_SIZE 1024

pthread_mutex_t lock;

typedef struct {
    char user_id[50];
    char directory[100];
} UserAccess;

UserAccess access_control[] = {
    {"Manufacturing_User", "/home/manufacturing"},
    {"Distribution_User", "/home/distribution"}
};

void trim_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';
}

int validate_user_id(const char *userID) {
    for (int i = 0; i < sizeof(access_control) / sizeof(UserAccess); i++) {
        if (strcmp(access_control[i].user_id, userID) == 0) {
            return i;
        }
    }
    return -1;
}

typedef struct {
    int socket_desc;
    struct sockaddr_in client;
} client_data;

void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    struct sockaddr_in addr;
    int addr_size = sizeof(addr);
    char buffer[BUFFER_SIZE], userID[BUFFER_SIZE], filePath[BUFFER_SIZE], content[BUFFER_SIZE * 4];
    int read_size, index;
    int file_fd;

    getpeername(sock, (struct sockaddr *)&addr, &addr_size);

    char logPath[512];
    sprintf(logPath, "/home/server_logs/server_log.txt");
    FILE* logFile = fopen(logPath, "a");
    if (!logFile) {
        perror("Failed to open log file");
        close(sock);
        free(socket_desc);
        return NULL;
    }

    // 현재 시간 구하기
    time_t t = time(NULL);
    struct tm *tm = localtime(&t); // tm 변수 초기화
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%c", tm);
    // with user ID: %s, userID
    fprintf(logFile, "[%s] Client connected from %s:%d \n", dateStr, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    while (1) {
        t = time(NULL);
        tm = localtime(&t);
        strftime(dateStr, sizeof(dateStr), "%c", tm);

        read_size = recv(sock, userID, BUFFER_SIZE, 0);
        if (read_size < 0) {
            fprintf(logFile, "[%s] Failed to receive userID from client\n", dateStr);
            break;
        }

        userID[read_size] = '\0';
        trim_whitespace(userID);

        index = validate_user_id(userID);
        if (index == -1) {
            send(sock, "Invalid user ID.\n", 17, 0);
            fprintf(logFile, "[%s] Client(%s:%d) Invalid userID received: %s\n", dateStr, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), userID);
            continue;
        } else {
            fprintf(logFile, "[%s] Client %s:%d validated with ID: %s\n", dateStr, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), userID);
            send(sock, "Valid user ID.", 14, 0);
        }

        read_size = recv(sock, filePath, BUFFER_SIZE, 0);
        if (read_size <= 0) {
            fprintf(logFile, "[%s] Failed to receive filePath from client\n", dateStr);
            break;
        }
        filePath[read_size] = '\0';

        read_size = recv(sock, content, BUFFER_SIZE * 4, 0);
        if (read_size <= 0) {
            fprintf(logFile, "[%s] Failed to receive content from client\n", dateStr);
            break;
        }
        content[read_size] = '\0';

        fprintf(logFile, "[%s] User %s edited file: %s\n", dateStr, userID, filePath);

        char fullPath[256];
        sprintf(fullPath, "%s/%s", access_control[index].directory, filePath);
        file_fd = open(fullPath, O_WRONLY | O_CREAT, 0666);
        if (file_fd > 0) {
            write(file_fd, content, strlen(content));
            close(file_fd);

            // 파일의 소유자 및 그룹 설정
            struct passwd *pwd = getpwnam(userID); // userID는 유효한 사용자 이름이어야 합니다.
            struct group *grp = getgrnam("manufacturing"); // 또는 "distribution" 등
            if (pwd && grp) {
                chown(fullPath, pwd->pw_uid, grp->gr_gid);
            } else {
                fprintf(stderr, "User or group not found.\n");
            }

            send(sock, "File uploaded successfully.\n", 28, 0);
            fprintf(logFile, "[%s] File updated to %s\n", dateStr, fullPath);
        } else {
            send(sock, "Error saving file.\n", 19, 0);
            fprintf(logFile, "[%s] Failed to save file at %s\n", dateStr, fullPath);
        }
    }

    // 클라이언트 연결 종료 메시지 출력
    t = time(NULL);
    tm = localtime(&t);
    strftime(dateStr, sizeof(dateStr), "%c", tm);
    fprintf(logFile, "[%s] Connection closed with client %s:%d\n", dateStr, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    printf("[%s] Client disconnected with IP: %s and port: %d\n", dateStr, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    fclose(logFile);
    close(sock);
    free(socket_desc);
    return NULL;
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server, client;
    socklen_t c;

    // 소켓 생성
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    // 서버 주소 구조체 준비
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    // 소켓에 주소 할당
    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed. Error");
        return 1;
    }

    // 연결 대기
    listen(server_sock, MAX_CONN);
    printf("Server listening on port %d...\n", PORT);

    c = sizeof(struct sockaddr_in);

    // 클라이언트 연결 수락
    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &c))) {
        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        if (new_sock == NULL) {
            perror("Failed to allocate memory");
            continue;
        }
        *new_sock = client_sock;

        // 클라이언트 처리를 위한 스레드 생성
        if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
        }

        printf("Handler assigned\n");
    }

    if (client_sock < 0) {
        perror("Accept failed");
        return 1;
    }

    // 소켓 닫기
    close(server_sock);
    return 0;
}