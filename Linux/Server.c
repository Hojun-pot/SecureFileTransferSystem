#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT 8080
#define MAX_CONN 10
#define BUFFER_SIZE 1024

pthread_mutex_t lock;

typedef struct {
    char user_id[50];
    char directory[100];
} UserAccess;

UserAccess access_control[] = {
    {"manufacturing_user", "/home/manufacturing"},
    {"distribution_user", "/home/distribution"}
};

void trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0)  return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
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

void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE], userID[BUFFER_SIZE], filePath[BUFFER_SIZE], content[BUFFER_SIZE * 4];
    int read_size, index;
    int file_fd;

    read_size = recv(sock, userID, BUFFER_SIZE, 0);
    if (read_size < 0) {
        close(sock);
        free(socket_desc);
        return NULL;
    }
    userID[read_size] = '\0';
    trim_whitespace(userID);
    index = validate_user_id(userID);
    if (index == -1) {
        send(sock, "Invalid user ID.\n", 17, 0);
    } else {
        send(sock, "Valid user ID.", 14, 0);
        read_size = recv(sock, filePath, BUFFER_SIZE, 0);
        filePath[read_size] = '\0';
        read_size = recv(sock, content, sizeof(content), 0);
        content[read_size] = '\0';
        char fullPath[256];
        sprintf(fullPath, "%s/%s", access_control[index].directory, filePath);

        // Set user and group ID
        uid_t uid = getpwnam(access_control[index].user_id)->pw_uid;
        gid_t gid = getpwnam(access_control[index].user_id)->pw_gid;

        file_fd = open(fullPath, O_WRONLY | O_CREAT, 0666);
        fchown(file_fd, uid, gid);
        if (file_fd > 0) {
            write(file_fd, content, strlen(content));
            close(file_fd);
            send(sock, "File uploaded successfully.\n", 28, 0);
        } else {
            send(sock, "Error saving file.\n", 19, 0);
        }
    }
    close(sock);
    free(socket_desc);
    return NULL;
}

int main() {
    int server_sock, client_sock, *new_sock;
    struct sockaddr_in server, client;
    socklen_t c;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    bind(server_sock, (struct sockaddr *)&server, sizeof(server));
    listen(server_sock, MAX_CONN);

    printf("Server listening on port %d...\n", PORT);
    c = sizeof(struct sockaddr_in);

    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &c))) {
        pthread_t thread_id;
        new_sock = malloc(1);
        *new_sock = client_sock;
        pthread_create(&thread_id, NULL, client_handler, (void*)new_sock);
    }
    return 0;
}
