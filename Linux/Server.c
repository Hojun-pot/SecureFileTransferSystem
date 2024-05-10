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
#include <sys/statvfs.h>
#include <stdbool.h>
#include <sys/stat.h> 
#include <errno.h>

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

void log_entry(FILE *logFile, const char *userID, const char *message) {
    time_t now = time(NULL);
    char *timestamp = ctime(&now);  // ctime() returns a string that ends with '\n'
    timestamp[strlen(timestamp)-1] = '\0'; // Remove the newline character

    fprintf(logFile, "[%s] (%s): %s\n", timestamp, userID, message);
}

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

int lock_file(int fd) {
    struct flock fl;
    fl.l_type = F_WRLCK;  // write lock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;  // lock entire file

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        perror("Locking file failed");
        return -1;
    }
    return 0;
}

int unlock_file(int fd) {
    struct flock fl;
    fl.l_type = F_UNLCK;  // unlock
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        perror("Unlocking file failed");
        return -1;
    }
    return 0;
}

bool check_extension(const char *file_path) {
    const char *extension = strrchr(file_path, '.');
    if (extension != NULL && strcmp(extension, ".txt") == 0) {
        return true;
    }
    return false;
}

int create_and_write_file(const char* file_path, const char* content, const char* user_id) {
    int file_fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0660);
    if (file_fd < 0) {
        perror("Failed to open or create file");
        return errno;  // Return the actual errno to the caller for specific handling
    }

    if (lock_file(file_fd) == -1) {
        perror("Failed to lock file");
        close(file_fd);
        return errno;
    }

    // Check if a newline is needed before appending new content
    if (lseek(file_fd, 0, SEEK_END) > 0) {
        if (write(file_fd, "\n", 1) < 0) {
            perror("Failed to write newline to file");
            unlock_file(file_fd);
            close(file_fd);
            return errno;
        }
    }

    int content_length = snprintf(NULL, 0, "%s (%s)", content, user_id) + 1;
    char* full_content = malloc(content_length);
    if (!full_content) {
        perror("Failed to allocate memory for content");
        unlock_file(file_fd);
        close(file_fd);
        return errno;
    }

    snprintf(full_content, content_length, "%s (%s)", content, user_id);
    if (write(file_fd, full_content, strlen(full_content)) < 0) {
        perror("Failed to write content to file");
        free(full_content);
        unlock_file(file_fd);
        close(file_fd);
        return errno;
    }

    free(full_content);
    unlock_file(file_fd);
    close(file_fd);
    return 0;
}


void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char userID[BUFFER_SIZE], filePath[BUFFER_SIZE], content[BUFFER_SIZE * 4];
    int read_size, index = -1;  // Initialize index to -1 to indicate no valid user initially

    pthread_mutex_lock(&lock);
    FILE *logFile = fopen("/home/server_logs/server_log.txt", "a");
    pthread_mutex_unlock(&lock);
    if (!logFile) {
        perror("Failed to open log file");
        close(sock);
        free(socket_desc);
        return NULL;
    }

    // Handle user ID
    while ((read_size = recv(sock, userID, BUFFER_SIZE, 0)) > 0) {
        userID[read_size] = '\0';
        trim_whitespace(userID);
        index = validate_user_group(userID);
        if (index != -1) {
            send(sock, "Valid user ID.", 14, 0);
            log_entry(logFile, userID, "Valid user ID received");
            break;
        } else {
            send(sock, "Invalid user ID. Check the ID again.", 36, 0);
            log_entry(logFile, userID, "Invalid user ID received");
            index = -1;  // Ensure index is reset to -1 to prevent further processing
            continue;  // Optionally continue prompting for ID, or remove to exit handler
        }
    }

    // Handle file path only if a valid user ID was confirmed
    if (index != -1) {
        while ((read_size = recv(sock, filePath, BUFFER_SIZE, 0)) > 0) {
            filePath[read_size] = '\0';
            trim_whitespace(filePath);
            if (check_extension(filePath)) {
                send(sock, "File path accepted.", 20, 0);
                log_entry(logFile, userID, "File path accepted");
                break;
            } else {
                send(sock, "Invalid file extension. Please enter a .txt file path.", 55, 0);
                log_entry(logFile, userID, "Invalid file extension");
            }
        }
    }

    // Handle file content and writing only if a valid file path was received
    if (index != -1 && strlen(filePath) > 0) {
        read_size = recv(sock, content, BUFFER_SIZE * 4, 0);
        if (read_size > 0) {
            content[read_size] = '\0';
            char fullPath[256];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", access_control[index].directory, filePath);
            int result = create_and_write_file(fullPath, content, userID);
            handle_file_creation_response(sock, logFile, userID, result, fullPath);
        } else {
            log_entry(logFile, userID, "Failed to receive content from client");
        }
    }

    fclose(logFile);
    close(sock);
    free(socket_desc);
    return NULL;
}

void handle_file_creation_response(int sock, FILE *logFile, const char *userID, int result, const char *fullPath) {
    char logMessage[512];
    switch (result) {
        case 0:
            send(sock, "File created and content written successfully.", 47, 0);
            snprintf(logMessage, sizeof(logMessage), "File written successfully: %s", fullPath);
            log_entry(logFile, userID, logMessage);
            break;
        case EACCES:
            send(sock, "Permission denied to write to file.", 36, 0);
            log_entry(logFile, userID, "Permission denied to write to file");
            break;
        case ENOENT:
            send(sock, "Directory does not exist.", 25, 0);
            log_entry(logFile, userID, "Directory does not exist");
            break;
        default:
            send(sock, "Failed to write to file due to an unknown error.", 49, 0);
            snprintf(logMessage, sizeof(logMessage), "Unknown error writing to file: %s", fullPath);
            log_entry(logFile, userID, logMessage);
            break;
    }
}


int main() {
    int server_sock, client_sock;
    struct sockaddr_in server, client;
    socklen_t c;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        return 1;
    }

    listen(server_sock, MAX_CONN);

    printf("Server listening on port %d...\n", PORT);
    c = sizeof(struct sockaddr_in);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client, &c);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, client_handler, (void*) new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
        }
    }

    return 0;
}
