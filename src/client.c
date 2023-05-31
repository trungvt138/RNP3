#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_BUFFER_SIZE 4096
#define EOT_CHAR 0x04

void sendCommand(int socket, const char* command) {
    // Send the command followed by the EOT character
    dprintf(socket, "%s%c", command, EOT_CHAR);
}

void receiveResponse(int socket) {
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytesRead;

    // Read data from the socket until EOT character is received
    while ((bytesRead = read(socket, buffer, sizeof(buffer))) > 0) {
        // Search for the EOT character in the received data
        char* eotPos = memchr(buffer, EOT_CHAR, bytesRead);
        if (eotPos != NULL) {
            // Calculate the number of bytes until the EOT character
            //ssize_t responseLength = eotPos - buffer;
            
            // Print the response
            if (write(STDOUT_FILENO, buffer, bytesRead) < 0) {
                perror("write");
                return;
            }
            break;
        }
        
        // Print the received data
        if (write(STDOUT_FILENO, buffer, bytesRead) < 0) {
            perror("write");
            return;
        }
    }
}

void receiveFile(int socket, const char* filename) {
    int file = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file < 0) {
        perror("File open");
        return;
    }

    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytesRead;

    // Read data from the socket until EOT character is received
    while ((bytesRead = read(socket, buffer, sizeof(buffer))) > 0) {
        // Search for the EOT character in the received data
        char* eotPos = memchr(buffer, EOT_CHAR, bytesRead);
        if (eotPos != NULL) {
            // Calculate the number of bytes until the EOT character
            //ssize_t fileDataLength = eotPos - buffer;
            
            // Write the file data to the file
            if (write(file, buffer, bytesRead) < 0) {
                perror("write");
                return;
            }
            break;
        }
        
        // Write the received data to the file
        if (write(file, buffer, bytesRead) < 0) {
            perror("write");
            return;
        }

    }

    close(file);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s <server IP> <server port>\n", argv[0]);
        return 1;
    }

    const char* serverIP = argv[1];
    int serverPort = atoi(argv[2]);

    int socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketfd < 0) {
        perror("Socket");
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    if (inet_pton(AF_INET, serverIP, &(serverAddr.sin_addr)) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if (connect(socketfd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Connect");
        return 1;
    }

    printf("Connected to server\n");

    // Receive and print the welcome message
    receiveResponse(socketfd);

    char command[256];
    while (1) {
        printf("Enter command: ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            perror("fgets");
            return 1;
        }

        // Remove the trailing newline character
        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0) {
            // Send the exit command and break out of the loop
            sendCommand(socketfd, command);
            break;
        }

        // Send the command to the server
        sendCommand(socketfd, command);

        // Check the command type and receive the appropriate response
        if (strncmp(command, "Get ", 4) == 0) {
            receiveFile(socketfd, command + 4);
        } else {
            receiveResponse(socketfd);
        }
    }

    close(socketfd);
    printf("Disconnected from server\n");

    return 0;
}
