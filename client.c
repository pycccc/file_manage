#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <termios.h>

#define PORT 12350
#define BUFFER_SIZE 512*1024
//set terminal mode so terminal buffer cant limit content size
void set_non_canonical_mode() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag &= ~(ICANON);
    t.c_cc[VMIN] = 1;       
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void reset_terminal_mode() {
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_lflag |= ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void handle_commands(int sockfd) {
    char content[BUFFER_SIZE / 2];
    char command[BUFFER_SIZE / 2];
    char buffer[BUFFER_SIZE];

    while (1) {
        printf("\n");
        printf("Enter command (create/read/write/mode/exit): ");
        memset(command, 0, sizeof(command));
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        command[strcspn(command, "\n")] = 0;

        if (strcmp(command, "exit") == 0) {
            
            send(sockfd, command, strlen(command), 0);
            printf("Exiting client.\n");
            break;
        }
        //send command to server 
        send(sockfd, command, strlen(command), 0);
   
        if (strncmp(command, "read", 4) == 0) {
            // keep receiving content until find "END OF FILE" 
            while (1) {
                memset(buffer, 0, sizeof(buffer));
                //receive from server
                int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_received <= 0) {
                    printf("Server disconnected.\n");
                    return; // end handle_commands()
                }
                buffer[bytes_received] = '\0';

                // find "END_OF_FILE" 
                char* end_marker = strstr(buffer, "END_OF_FILE");
                if (end_marker != NULL) {
                    // convert "END_OF_FILE" to '\0'
                    *end_marker = '\0';
                    printf("%s", buffer);
                    break; // back to "enter command..."
                }
                else {
                    // if no "END_OF_FILE" , just print content
                    printf("%s", buffer);
                }
            }
        }
        else {
            // create/mode/write operation
            memset(buffer, 0, sizeof(buffer));
            int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received <= 0) {
                printf("Server disconnected.\n");
                break;
            }
            buffer[bytes_received] = '\0';
            printf("%s", buffer);

            if (strncmp(command, "write", 5) == 0 && strstr(buffer, "Enter your content")) {
                
                set_non_canonical_mode();
                while (1) {
                    memset(content, 0, sizeof(content));
                    if (!fgets(content, sizeof(content), stdin)) {
                        printf("Error reading input.\n");
                        break;
                    }
                    content[strcspn(content, "\n")] = '\0';
                    if (strlen(content) == 0) {
                        send(sockfd, "\n", 1, 0);
                        break;
                    }
                    if (send(sockfd, content, strlen(content), 0) == -1) {
                        perror("Error sending data to server");
                        break;
                    }
                }
                reset_terminal_mode();
                // receive the response of write command from server
                memset(buffer, 0, sizeof(buffer));
                bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("%s", buffer);
                }
                else {
                    printf("Server disconnected.\n");
                    break;
                }
            }
        }
    }
}
int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char username[20], group[20];
    int group_choice;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connection to server failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server.\n");
    printf("\n");
    printf("Enter username: ");
    if (!fgets(username, sizeof(username), stdin)) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    username[strcspn(username, "\n")] = 0;
    printf("\n");
    printf("Select group:\n");
    printf("1. AOS-students\n");
    printf("2. CSE-students\n");
    printf("Enter your choice (1 or 2): ");
    if (scanf("%d", &group_choice) != 1) {
        printf("Invalid input.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    getchar(); // Consume newline left by scanf

    if (group_choice == 1) {
        strcpy(group, "AOS-students");
    }
    else if (group_choice == 2) {
        strcpy(group, "CSE-students");
    }
    else {
        printf("Invalid choice. Exiting.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    snprintf(buffer, sizeof(buffer), "%s|%s", username, group);
    send(sockfd, buffer, strlen(buffer), 0);
    // receive group validation outcome
    memset(buffer, 0, sizeof(buffer));
    int bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        printf("Server disconnected.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[bytes_received] = '\0';
    printf("%s", buffer);
    if (strstr(buffer, "Invalid group")) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("Acceptable commands:\n");
    printf("1. create <filename> <permission>\n");
    printf("2. read <filename>\n");
    printf("3. write <filename> o/a\n");
    printf("4. mode <filename> <permission>\n");
    
    handle_commands(sockfd);
    close(sockfd);
    return 0;
}
