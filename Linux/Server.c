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
    // Open the file with the appropriate flags and permissions
    int file_fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0660);
    if (file_fd < 0) {
        switch(errno) {
            case EACCES:
                perror("Failed to open or create file: Access denied");
                break;
            case ENOENT:
                perror("Failed to open or create file: Directory does not exist");
                break;
            default:
                perror("Failed to open or create file for an unknown reason");
        }
        return -1;
    }

    // Attempt to lock the file immediately after opening
    if (lock_file(file_fd) == -1) {
        close(file_fd); // Make sure to close the file descriptor if locking fails
        return -1;
    }

    // Check if file is empty and if not, add a newline
    off_t current_size = lseek(file_fd, 0, SEEK_END);
    if (current_size > 0) {
        if (write(file_fd, "\n", 1) < 0) {
            perror("Failed to write space to file");
            unlock_file(file_fd);
            close(file_fd);
            return -1;
        }
    }

    // Prepare content to be written including user ID
    int content_length = strlen(content) + strlen(user_id) + 3; // content + user ID + parentheses and null terminator
    char* full_content = malloc(content_length);
    if (full_content == NULL) {
        perror("Failed to allocate memory for full content");
        unlock_file(file_fd);
        close(file_fd);
        return -1;
    }

    sprintf(full_content, "%s (%s)", content, user_id);

    // Write the prepared content to the file
    if (write(file_fd, full_content, strlen(full_content)) < 0) {
        perror("Failed to write to file");
        free(full_content);
        unlock_file(file_fd);
        close(file_fd);
        return -1;
    }

    free(full_content);
    unlock_file(file_fd);
    close(file_fd); // Close the file descriptor after unlocking
    return 0; // Return success
}

void *client_handler(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char userID[BUFFER_SIZE], filePath[BUFFER_SIZE], content[BUFFER_SIZE * 4];
    int read_size, index;

    // Lock and open the log file
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

    // User ID validation loop
    while (1) {
        log_entry(logFile, userID, "Awaiting file path from client...");
        read_size = recv(sock, userID, BUFFER_SIZE, 0);
        if (read_size <= 0) {
           log_entry(logFile, userID, "Connection lost or client disconnected\n");
            break;
        }
        userID[read_size] = '\0';
        trim_whitespace(userID);
        log_entry(logFile, "Received user ID: %s\n", userID);
        index = validate_user_group(userID);
        if (index != -1) {
            log_entry(logFile, userID, "Valid user ID received\n");
            send(sock, "Valid user ID.", 14, 0);
            break;
        } else {
            send(sock, "Invalid user ID. Check the ID again.", 30, 0);
        }
    }

    // File path entry loop
    if (index != -1) {
        while (1) {
            read_size = recv(sock, filePath, BUFFER_SIZE, 0);
            if (read_size <= 0) {
                log_entry(logFile, userID, "Failed to receive filePath from client\n");
                break;
            }
            filePath[read_size] = '\0';
            trim_whitespace(filePath);
            char logMessage[512]; // Ensure this buffer is large enough for your needs
            // After receiving filePath from client
            snprintf(logMessage, sizeof(logMessage), "Received file path: %s", filePath);
            log_entry(logFile, userID, logMessage);
            if (check_extension(filePath)) {
                send(sock, "File path accepted.\n", 20, 0);
                log_entry(logFile, userID, "Valid file extension received.\n");
                break;
            } else {
                send(sock, "Invalid file extension. Please enter a .txt file path.\n", 60, 0);
                log_entry(logFile, userID, "Invalid file extension received.\n");
            }
        }
    }

    // File content receiving and writing
    if (index != -1 && strlen(filePath) > 0) {
        read_size = recv(sock, content, BUFFER_SIZE * 4, 0);
        if (read_size > 0) {
            content[read_size] = '\0';
            char fullPath[256];
            sprintf(fullPath, "%s/%s", access_control[index].directory, filePath);
            char logMessage[512]; // Ensure this buffer is large enough for your needs
            // After receiving filePath from client
            if (create_and_write_file(fullPath, content, userID) == 0) {
                send(sock, "File created and content written successfully.\n", 47, 0);
                snprintf(logMessage, sizeof(logMessage), "File written successfully. %s", filePath);
                log_entry(logFile, userID, logMessage);
            } else {
                send(sock, "Failed to open file.\n", 21, 0);
                snprintf(logMessage, sizeof(logMessage), "Failed to open file. %s", filePath);
                log_entry(logFile, userID, logMessage); 
            }
        } else {
            log_entry(logFile,userID, "Failed to receive content from client\n");
        }
    }

    fclose(logFile);
    close(sock);
    free(socket_desc);
    return NULL;
}


int main() {
    int server_sock, client_sock, c;
    struct sockaddr_in server, client;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
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
        client_sock = accept(server_sock, (struct sockaddr *)&client, (socklen_t*)&c);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Client connected with IP: %s and port: %d\n", inet_ntoa(client.sin_addr), ntohs(client.sin_port));

        pthread_t thread_id;
        int *new_sock = malloc(sizeof(int));
        if (!new_sock) {
            perror("Failed to allocate memory");
            continue;
        }
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, client_handler, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
            continue;
        }
    }

    close(server_sock);
    return 0;
}
