#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PORT 12350
#define BUFFER_SIZE 512*1024
#define COMMAND_BUFFER_SIZE 512
#define MAX_CLIENTS 10
#define RESPONSE_SIZE 65536

const char* GROUPS[] = { "AOS-students", "CSE-students" };

pthread_mutex_t file_system_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct File {
    char filename[50];
    char owner[20];
    char group[20];
    char permissions[7];
    int size;
    char creation_date[20]; 
    char* contentBuffer;    // dynamic allocate content    
    //each file has its mutex lock
    pthread_mutex_t rwmutex; 
    int active_readers;
    int active_writers;
} File;

File file_system[100];
int file_count = 0;
//check "AOS-students", "CSE-students" or else
int is_valid_group(const char* group) {
    for (int i = 0; i < (int)(sizeof(GROUPS) / sizeof(GROUPS[0])); i++) {
        if (strcmp(group, GROUPS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
// mutex lock
void init_file_lock(File* f) {
    pthread_mutex_init(&(f->rwmutex), NULL);
    f->active_readers = 0;
    f->active_writers = 0;
}

int try_start_read(File* f) {
    pthread_mutex_lock(&(f->rwmutex));
    if (f->active_writers > 0) {

        pthread_mutex_unlock(&(f->rwmutex));
        return 0;
    }
    f->active_readers++;
    pthread_mutex_unlock(&(f->rwmutex));
    return 1;
}

void end_read(File* f) {
    pthread_mutex_lock(&(f->rwmutex));
    f->active_readers--;
    pthread_mutex_unlock(&(f->rwmutex));
}

int try_start_write(File* f) {
    pthread_mutex_lock(&(f->rwmutex));
    if (f->active_writers > 0 || f->active_readers > 0) {
        pthread_mutex_unlock(&(f->rwmutex));
        return 0;
    }
    f->active_writers = 1;
    pthread_mutex_unlock(&(f->rwmutex));
    return 1;
}

void end_write(File* f) {
    pthread_mutex_lock(&(f->rwmutex));
    f->active_writers = 0;
    pthread_mutex_unlock(&(f->rwmutex));
}

void add_file(const char* filename, const char* owner, const char* group, const char* permissions, int size) {
    pthread_mutex_lock(&file_system_lock);
    // initialize
    strncpy(file_system[file_count].filename, filename, sizeof(file_system[file_count].filename));
    strncpy(file_system[file_count].owner, owner, sizeof(file_system[file_count].owner));
    strncpy(file_system[file_count].group, group, sizeof(file_system[file_count].group));
    strncpy(file_system[file_count].permissions, permissions, sizeof(file_system[file_count].permissions));
    file_system[file_count].size = 0;

    // allocate space to content buffer
    file_system[file_count].contentBuffer = (char*)malloc(size);
    if (file_system[file_count].contentBuffer == NULL) {
        perror("Failed to allocate memory for contentBuffer");
        pthread_mutex_unlock(&file_system_lock);
        exit(EXIT_FAILURE);
    }
    memset(file_system[file_count].contentBuffer, 0, size);

    // for capability list date
    time_t t = time(NULL);
    struct tm* tm_info = localtime(&t);
    strftime(file_system[file_count].creation_date, sizeof(file_system[file_count].creation_date), "%b %d %Y", tm_info);

    init_file_lock(&file_system[file_count]);

    file_count++;
    pthread_mutex_unlock(&file_system_lock);
}

void print_capability_list() {
    pthread_mutex_lock(&file_system_lock);
    printf("\nCapability List:\n");
    printf("Permissions Owner     Group     Size     Date          Filename\n");
    printf("----------------------------------------------------------------\n");

    for (int i = 0; i < file_count; i++) {
        printf("%-10s %-10s %-10s %-8d %-12s %s\n",
            file_system[i].permissions,
            file_system[i].owner,
            file_system[i].group,
            file_system[i].size,
            file_system[i].creation_date,
            file_system[i].filename);
    }

    printf("----------------------------------------------------------------\n");
    pthread_mutex_unlock(&file_system_lock);
}

int has_permission(const File* file, const char* username, const char* group, const char* operation) {
    if (strcmp(file->owner, username) == 0) {
        //owner permission
        if (strcmp(operation, "read") == 0 && file->permissions[0] == 'r') {
            return 1;
        }
        if (strcmp(operation, "write") == 0 && file->permissions[1] == 'w') {
            return 1;
        }
    }//group permission
    else if (strcmp(file->group, group) == 0) {
        if (strcmp(operation, "read") == 0 && file->permissions[2] == 'r') {
            return 1;
        }
        if (strcmp(operation, "write") == 0 && file->permissions[3] == 'w') {
            return 1;
        }
    }
    else {
        // Others permission
        if (strcmp(operation, "read") == 0 && file->permissions[4] == 'r') {
            return 1;
        }
        if (strcmp(operation, "write") == 0 && file->permissions[5] == 'w') {
            return 1;
        }
    }
    return 0; 
}


void* handle_client(void* arg) {
    int client_socket = (intptr_t)arg;
    char command_buffer[COMMAND_BUFFER_SIZE];
    char content_buffer[BUFFER_SIZE];
    char response[RESPONSE_SIZE];
    char username[20], group[20];
    //receive username+group
    memset(command_buffer, 0, sizeof(command_buffer));
    if (recv(client_socket, command_buffer, sizeof(command_buffer), 0) <= 0) {
        perror("Failed to receive username/group");
        close(client_socket);
        return NULL;
    }
    // command_buffer = "username|name";
    char* token = strtok(command_buffer, "|");
    if (token != NULL) {
        strncpy(username, token, sizeof(username));
        username[sizeof(username) - 1] = '\0';
        token = strtok(NULL, "|");
    }
    if (token != NULL) {
        strncpy(group, token, sizeof(group));
        group[sizeof(group) - 1] = '\0';
    }
    else {
        strcpy(group, "");
    }
    printf("Received username: '%s'\n", username);
    printf("Received group: '%s'\n", group);

    // check group 
    memset(response, 0, RESPONSE_SIZE);
    if (!is_valid_group(group)) {
        snprintf(response, RESPONSE_SIZE, "Invalid group\n");
        send(client_socket, response, strlen(response), 0);
        close(client_socket);
        return NULL;
    }
    else {
        snprintf(response, RESPONSE_SIZE, "\n");
        send(client_socket, response, strlen(response), 0);
    }

    while (1) {
        memset(response, 0, RESPONSE_SIZE);
        memset(command_buffer, 0, COMMAND_BUFFER_SIZE);
        memset(content_buffer, 0, BUFFER_SIZE);

        int bytes_read = recv(client_socket, command_buffer, sizeof(command_buffer), 0);
        if (bytes_read <= 0) {
            printf("Client disconnected: %s\n", username);
            break;
        }
        command_buffer[bytes_read] = '\0';
        printf("Received raw command: '%s'\n", command_buffer);
        //analyze command
        char command[10] = { 0 }, filename[50] = { 0 }, permissions[7] = { 0 }, mode[2] = { 0 };
        int matched = sscanf(command_buffer, "%s %s %s", command, filename, permissions);

        if (strcmp(command, "create") == 0) {
            // create <filename> <permission>
            if (matched != 3 || strlen(filename) == 0 || strlen(permissions) != 6 || strspn(permissions, "rw-") != 6) {
                snprintf(response, RESPONSE_SIZE, "Invalid command. Usage: create <filename> <rwrwrw>.\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            // check if file is exist
            int file_exists = 0;
            pthread_mutex_lock(&file_system_lock);
            for (int i = 0; i < file_count; i++) {
                if (strcmp(file_system[i].filename, filename) == 0) {
                    file_exists = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&file_system_lock);

            if (file_exists) {
                snprintf(response, RESPONSE_SIZE, "File %s already exists.\n", filename);
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            add_file(filename, username, group, permissions, BUFFER_SIZE);
            snprintf(response, RESPONSE_SIZE, "File created successfully.\n");
            send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(command, "mode") == 0) {
            // mode <filename> <new_permissions>
            if (matched != 3 || strlen(filename) == 0 || strlen(permissions) != 6 || strspn(permissions, "rw-") != 6) {
                snprintf(response, RESPONSE_SIZE, "Invalid command. Usage: mode <filename> <rwrwrw>.\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            //find the file
            int file_found = 0;
            int file_index = -1;
            pthread_mutex_lock(&file_system_lock);
            for (int i = 0; i < file_count; i++) {
                if (strcmp(file_system[i].filename, filename) == 0) {
                    file_found = 1;
                    file_index = i;
                    break;
                }
            }
            pthread_mutex_unlock(&file_system_lock);

            if (!file_found) {
                snprintf(response, RESPONSE_SIZE, "file not exist\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            //check is owner or not
            if (strcmp(file_system[file_index].owner, username) != 0 || strcmp(file_system[file_index].group, group) != 0) {
                snprintf(response, RESPONSE_SIZE, "Permission denied: You are not the owner.\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }

            pthread_mutex_lock(&file_system_lock);
            strncpy(file_system[file_index].permissions, permissions, 6);
            file_system[file_index].permissions[6] = '\0';
            pthread_mutex_unlock(&file_system_lock);

            snprintf(response, RESPONSE_SIZE, "Permissions of file %s updated successfully.\n", filename);
            send(client_socket, response, strlen(response), 0);
        }
        else if (strcmp(command, "write") == 0) {
            int writeFormat = sscanf(command_buffer, "%s %s %s", command, filename, mode);
            //write <filename> o/a
            if (writeFormat != 3 || (strcmp(mode, "o") != 0 && strcmp(mode, "a") != 0)) {
                snprintf(response, RESPONSE_SIZE, "Invalid command. Usage: write <filename> <o/a>.\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            int file_found = 0;
            int file_index = -1;
            pthread_mutex_lock(&file_system_lock);
            for (int i = 0; i < file_count; i++) {
                if (strcmp(file_system[i].filename, filename) == 0) {
                    file_found = 1;
                    file_index = i;
                    break;
                }
            }
            pthread_mutex_unlock(&file_system_lock);

            if (!file_found) {
                snprintf(response, RESPONSE_SIZE, "File %s not found.\n", filename);
                send(client_socket, response, strlen(response), 0);
                continue;
            }

            File* target_file = &file_system[file_index];

            if (!try_start_write(target_file)) {
                snprintf(response, RESPONSE_SIZE, "Other client is reading or writing this file.\n");
                send(client_socket, response, strlen(response), 0);
                continue;
            }

            if (!has_permission(target_file, username, group, "write")) {
                snprintf(response, RESPONSE_SIZE, "Permission denied: You cannot write to file %s.\n", filename);
                send(client_socket, response, strlen(response), 0);
                end_write(target_file);
                continue;
            }

            snprintf(response, RESPONSE_SIZE, "Enter your content. End with an empty line:\n");
            send(client_socket, response, strlen(response), 0);

            //start to receive content
            int received = 0;
            // receive lines of content from server
            char temp_buffer[BUFFER_SIZE / 2];
            while (1) {
                memset(temp_buffer, 0, sizeof(temp_buffer));
                int bytes = recv(client_socket, temp_buffer, sizeof(temp_buffer) - 1, 0);
                if (bytes <= 0) {
                    printf("Client disconnected during write: %s\n", username);
                    end_write(target_file);
                    break;
                }
                temp_buffer[bytes] = '\0';


                if (strcmp(temp_buffer, "\n") == 0 || strcmp(temp_buffer, "\r\n") == 0) {
                    break;
                }
                
                pthread_mutex_lock(&file_system_lock);
                int required_size = target_file->size + bytes;
                if (required_size > BUFFER_SIZE) {
                    //if content size is larger than space
                    char* new_buffer = realloc(target_file->contentBuffer, required_size);
                    if (!new_buffer) {
                        snprintf(response, RESPONSE_SIZE, "Failed to allocate memory for content.\n");
                        send(client_socket, response, strlen(response), 0);
                        pthread_mutex_unlock(&file_system_lock);
                        end_write(target_file);
                        break;
                    }
                    target_file->contentBuffer = new_buffer;
                }
                if (strcmp(mode, "o") == 0 && received == 0) {      
                    memset(target_file->contentBuffer, 0, target_file->size);
                    target_file->size = 0;
                }
                strncat(target_file->contentBuffer, temp_buffer, bytes);
                target_file->size += bytes;
                pthread_mutex_unlock(&file_system_lock);
                received += bytes;
            }

            snprintf(response, RESPONSE_SIZE, "File %s written successfully with %d bytes.\n", filename, received);
            send(client_socket, response, strlen(response), 0);
            end_write(target_file);
        }
        else if (strcmp(command, "read") == 0) {
            int readFormat = sscanf(command_buffer, "%s %s", command, filename);
            //read <filename>
            if (readFormat != 2 || strlen(filename) == 0) {
                snprintf(response, RESPONSE_SIZE, "Invalid command. Usage: read <filename>.\nEND_OF_FILE");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            int file_found = 0;
            int file_index = -1;
            pthread_mutex_lock(&file_system_lock);
            for (int i = 0; i < file_count; i++) {
                if (strcmp(file_system[i].filename, filename) == 0) {
                    file_found = 1;
                    file_index = i;
                    break;
                }
            }
            pthread_mutex_unlock(&file_system_lock);

            if (!file_found) {
                snprintf(response, RESPONSE_SIZE, "File %s not found.\nEND_OF_FILE", filename);
                send(client_socket, response, strlen(response), 0);
                continue;
            }

            File* target_file = &file_system[file_index];

            if (!try_start_read(target_file)) {
                snprintf(response, RESPONSE_SIZE, "Other client is writing this file\nEND_OF_FILE");
                send(client_socket, response, strlen(response), 0);
                continue;
            }
            if (!has_permission(target_file, username, group, "read")) {
                snprintf(response, RESPONSE_SIZE, "Permission denied: You cannot read file %s.\nEND_OF_FILE", filename);
                send(client_socket, response, strlen(response), 0);
                end_read(target_file);
                continue;
            }
            if (target_file->size == 0) {               
                snprintf(response, RESPONSE_SIZE, "File %s is empty.\nEND_OF_FILE", filename);
            }
            else {   
                sleep(2); // simulate reading delay
             
                int bytes_sent = 0;
                while (bytes_sent < target_file->size) {
                    //calculate size of part of content
                    int chunk_size = RESPONSE_SIZE - 1; // save 1 bit for '\0'
                    if (bytes_sent + chunk_size > target_file->size) {
                        chunk_size = target_file->size - bytes_sent;
                    }
                    // send part of content
                    snprintf(response, chunk_size + 1, "%s", target_file->contentBuffer + bytes_sent);
                    send(client_socket, response, chunk_size, 0);
                    bytes_sent += chunk_size;
                }
                // end with "END_OF_FILE"
                snprintf(response, RESPONSE_SIZE, "END_OF_FILE");
            }
            send(client_socket, response, strlen(response), 0);
            end_read(target_file);
        }
        else if (strcmp(command, "exit") == 0) {
            printf("Client exited: %s\n", username);
            send(client_socket, response, strlen(response), 0);
            break;
        }
        else {
            snprintf(response, RESPONSE_SIZE, "Invalid command.\n");
            send(client_socket, response, strlen(response), 0);
        }
        print_capability_list();
    }
    close(client_socket);
    return NULL;
}

void cleanup_file_system() {
    pthread_mutex_lock(&file_system_lock);
    for (int i = 0; i < file_count; i++) {
        if (file_system[i].contentBuffer) {
            free(file_system[i].contentBuffer);
            file_system[i].contentBuffer = NULL;
        }
    }
    pthread_mutex_unlock(&file_system_lock);
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size = sizeof(client_addr);
    int active_clients = 0;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", PORT);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
        if (client_socket == -1) {
            perror("Client connection failed");
            continue;
        }
        if (active_clients >= MAX_CLIENTS) {
            printf("Max clients reached. Rejecting connection.\n");
            close(client_socket);
            continue;
        }
        active_clients++;
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, (void*)(intptr_t)client_socket) != 0) {
            perror("Thread creation failed");
            close(client_socket);
            active_clients--;
        }
        pthread_detach(thread);
    }
    cleanup_file_system();
    close(server_socket);
    return 0;
}
