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
#include <ctype.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CONN 10
#define BUFFER_SIZE 1024

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

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
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

int validate_user_group(const char *userID) {
    struct passwd *pwd = getpwnam(userID);
    if (pwd == NULL) {
        return -1;
    }
    gid_t user_gid = pwd->pw_gid;

    gid_t groups[50];
    int ngroups = 50;
    if (getgrouplist(userID, user_gid, groups, &ngroups) == -1) {
        perror("getgrouplist");
        return -1;
    }

    for (int i = 0; i < sizeof(access_control) / sizeof(UserAccess); i++) {
        struct group *grp = getgrgid(groups[i]);
        if (grp != NULL) {
            for (int j = 0; j < ngroups; j++) {
                if (groups[j] == grp->gr_gid) {
                    return i;
                }
            }
        }
    }
    return -1;
}


void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char userID[BUFFER_SIZE], filePath[BUFFER_SIZE], content[BUFFER_SIZE * 4];
    int read_size, index;

    pthread_mutex_lock(&lock);
    char logPath[512] = "/home/server_logs/server_log.txt";
    FILE *logFile = fopen(logPath, "a");
    pthread_mutex_unlock(&lock);

    if (!logFile) {
        perror("Failed to open log file");
        close(sock);
        free(socket_desc);
        return NULL;
    }

    while (1) {
    read_size = recv(sock, userID, BUFFER_SIZE, 0);
    if (read_size <= 0) {
        fprintf(logFile, "Connection lost or client disconnected\n");
        break;
    }
    userID[read_size] = '\0';
    trim_whitespace(userID);

    index = validate_user_group(userID);
    while (index == -1) {
        send(sock, "Invalid user ID. Enter again: ", 30, 0);
        read_size = recv(sock, userID, BUFFER_SIZE, 0);
        if (read_size <= 0) {
            fprintf(logFile, "Connection lost or client disconnected\n");
            break;
        }
        userID[read_size] = '\0';
        trim_whitespace(userID);
        index = validate_user_group(userID);
    }

        fprintf(logFile, "Valid user ID received: %s\n", userID);
        send(sock, "Valid user ID.", 14, 0);

        read_size = recv(sock, filePath, BUFFER_SIZE, 0);
        if (read_size <= 0) {
            fprintf(logFile, "Failed to receive filePath from client\n");
            break;
        }
        filePath[read_size] = '\0';

        read_size = recv(sock, content, BUFFER_SIZE * 4, 0);
        if (read_size <= 0) {
            fprintf(logFile, "Failed to receive content from client\n");
            break;
        }
        content[read_size] = '\0';

        char fullPath[256];
        sprintf(fullPath, "%s/%s", access_control[index].directory, filePath);

        // 파일 경로 확인
        if (access(fullPath, F_OK) != 0) {
            perror("File path does not exist or is incorrect");
        }

        // 쓰기 권한 확인
        if (access(fullPath, W_OK) != 0) {
            perror("No write permission to the file");
        }

        // 디스크 공간 확인
        struct statvfs stat;
        if (statvfs(fullPath, &stat) != 0) {
            perror("Failed to get file system information");
        } else if (stat.f_bavail * stat.f_bsize < strlen(content)) {
            fprintf(stderr, "Not enough disk space\n");
        }
        
        int file_fd = open(fullPath, O_WRONLY | O_CREAT, 0666);
        if (file_fd > 0) {
            write(file_fd, content, strlen(content));
            close(file_fd);

            struct passwd *pwd = getpwnam(access_control[index].user_id);
            struct group *grp = getgrnam((index == 0) ? "Manufacturing" : "Distribution");
            if (pwd && grp) {
                chown(fullPath, pwd->pw_uid, grp->gr_gid);
            } else {
                fprintf(logFile, "User or group not found.\n");
            }

            send(sock, "File uploaded successfully.\n", 28, 0);
            fprintf(logFile, "File updated to %s\n", fullPath);
        } else {
            perror("Error saving file");
            fprintf(logFile, "Failed to save file at %s\n", fullPath);
        }

        break; // Exit after handling one client for simplicity
    }

    fclose(logFile);
    close(sock);
    free(socket_desc);
    return NULL;
}

int main() {
    int server_sock, client_sock, c;
    int opt = 1;
    struct sockaddr_in server, client;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create socket");
        return 1;
    }
    // SO_REUSEADDR 옵션 설정
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        return 1;
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed. Error");
        return 1;
    }

    listen(server_sock, MAX_CONN);
    printf("Server listening on port %d...\n", PORT);

    c = sizeof(struct sockaddr_in);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client, (socklen_t*)&c);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected with IP: %s and port: %d\n", client_ip, ntohs(client.sin_port));
        
        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        if (!new_sock) {
            perror("Failed to allocate memory");
            continue;
        }
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock) < 0) {
            perror("could not create thread");
            return 1;
        }
    }

    close(server_sock);
    return 0;
}
