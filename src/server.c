#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#define DEFAULT_PORT 0
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define EOT_CHAR '\x04'

typedef struct {
  char hostname[256];
  int port;
} ClientInfo;

void sendStream(int clientSocket, const char* data, ssize_t dataSize) {
  ssize_t bytesSent;
  ssize_t totalSent = 0;

  while (totalSent < dataSize) {
    bytesSent = send(clientSocket, data + totalSent, dataSize - totalSent, 0);
    if (bytesSent < 0) {
      perror("Send");
      return;
    }
    totalSent += bytesSent;
  }
}

void handleCommand(int clientSocket, int* clientSockets, const char* command) {
  if (strcmp(command, "List") == 0) {
    // Create an array to store the client information
    ClientInfo clients[MAX_CLIENTS];
    int numClients = 0;

    // Loop through all connected sockets and get client information
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clientSockets[i] != -1) {
        struct sockaddr_in clientAddr;
        unsigned int addrLen = sizeof(clientAddr);
        getpeername(clientSockets[i], (struct sockaddr*)&clientAddr, &addrLen);

        // Store client hostname and port in the array
        strcpy(clients[numClients].hostname, inet_ntoa(clientAddr.sin_addr));
        clients[numClients].port = ntohs(clientAddr.sin_port);
        numClients++;
      }
    }

    // Create the response string
    char response[256];
    sprintf(response, "Connected Clients:\n");
    for (int i = 0; i < numClients; i++) {
      char clientInfo[256];
      sprintf(clientInfo, "%s:%d\n", clients[i].hostname, clients[i].port);
      strcat(response, clientInfo);
    }
    sprintf(response + strlen(response), "Total Clients: %d\n", numClients);

    // Send the response back to the client as a stream
    sendStream(clientSocket, response, strlen(response));
    sendStream(clientSocket, "\n", 1);
    sendStream(clientSocket, &EOT_CHAR, 1);
  }
  else if (strcmp(command, "Files") == 0) {
    // Files command implementation

    // Open the server directory
    DIR* dir;
    struct dirent* entry;
    struct stat fileStat;
    char fileAttributes[256];
    int numFiles = 0;

    dir = opendir(".");
    if (dir == NULL) {
      perror("opendir");
      return;
    }

    // Prepare the response string
    char response[4096] = "List of Files:\n";

    // Read directory entries and get file information
    while ((entry = readdir(dir)) != NULL) {
      if (stat(entry->d_name, &fileStat) < 0) {
        perror("stat");
        continue;
      }

      // Format the file attributes
      strftime(fileAttributes, sizeof(fileAttributes), "%Y-%m-%d %H:%M:%S", localtime(&fileStat.st_mtime));

      // Append the file information to the response string
      char fileInfo[512];
      sprintf(fileInfo, "%s - Size: %ld bytes, Last Modified: %s\n", entry->d_name, fileStat.st_size, fileAttributes);
      strcat(response, fileInfo);
      numFiles++;
    }

    // Close the directory
    closedir(dir);

    // Append the total number of files to the response string
    sprintf(response + strlen(response), "Total Files: %d\n", numFiles);

    // Send the response back to the client as a stream
    sendStream(clientSocket, response, strlen(response));
    sendStream(clientSocket, "\n", 1);
    sendStream(clientSocket, &EOT_CHAR, 1);
  }
  else if (strncmp(command, "Get", 3) == 0) {
    // Get command implementation
    char fileName[256];
    char fileAttributes[256];

    // Extract the file name from the command
    sscanf(command, "Get %s", fileName);

    // Open the file in binary mode for reading
    FILE* file = fopen(fileName, "rb");
    if (file == NULL) {
      perror("fopen");
      sendStream(clientSocket, "File not found.\n", 16);
      sendStream(clientSocket, EOT_CHAR, 1);
      return;
    }

    // Get file size and last modified time
    struct stat fileStat;
    if (stat(fileName, &fileStat) < 0) {
      perror("stat");
      fclose(file);
      return;
    }

    // Format the file attributes
    strftime(fileAttributes, sizeof(fileAttributes), "%Y-%m-%d %H:%M:%S", localtime(&fileStat.st_mtime));

    // Send the file attributes as a stream
    sendStream(clientSocket, fileAttributes, strlen(fileAttributes));
    sendStream(clientSocket, "\n", 1);

    // Send the file contents as a stream
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      sendStream(clientSocket, buffer, bytesRead);
    }

    fclose(file);

    // Send the EOT character to mark the end of the stream
    sendStream(clientSocket, &EOT_CHAR, 1);
  }
}

int main() {
  // Create a TCP socket
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("Socket");
    return 1;
  }

  // Set up the server address
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(DEFAULT_PORT);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  // Bind the socket to the server address
  if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
    perror("Bind");
    close(serverSocket);
    return 1;
  }

  // Listen for incoming connections
  if (listen(serverSocket, MAX_CLIENTS) < 0) {
    perror("Listen");
    close(serverSocket);
    return 1;
  }

  // Initialize client socket array
  int clientSockets[MAX_CLIENTS];
  for (int i = 0; i < MAX_CLIENTS; i++) {
    clientSockets[i] = -1;
  }

  // Set of socket descriptors
  fd_set readfds;
  int maxSocket = serverSocket;

  while (1) {
    // Clear the socket set
    FD_ZERO(&readfds);

    // Add server socket to the set
    FD_SET(serverSocket, &readfds);

    // Add child sockets to the set
    for (int i = 0; i < MAX_CLIENTS; i++) {
      int socket = clientSockets[i];
      if (socket != -1) {
        FD_SET(socket, &readfds);
        if (socket > maxSocket) {
          maxSocket = socket;
        }
      }
    }

    // Wait for activity on any of the sockets
    if (select(maxSocket + 1, &readfds, NULL, NULL, NULL) < 0) {
      perror("Select");
      close(serverSocket);
      return 1;
    }

    // Check for incoming connection on the server socket
    if (FD_ISSET(serverSocket, &readfds)) {
      struct sockaddr_in clientAddr;
      socklen_t clientAddrLen = sizeof(clientAddr);
      int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
      if (clientSocket < 0) {
        perror("Accept");
        close(serverSocket);
        return 1;
      }

      // Add new client socket to the array
      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientSockets[i] == -1) {
          clientSockets[i] = clientSocket;
          break;
        }
      }
    }

    // Check for activity on any of the client sockets
    for (int i = 0; i < MAX_CLIENTS; i++) {
      int clientSocket = clientSockets[i];
      if (clientSocket != -1 && FD_ISSET(clientSocket, &readfds)) {
        char buffer[BUFFER_SIZE];
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
          // Connection closed or error occurred, remove the client socket from the array
          close(clientSocket);
          clientSockets[i] = -1;
        } else {
          // Handle the received command
          buffer[bytesRead] = '\0';
          handleCommand(clientSocket, clientSockets, buffer);
        }
      }
    }
  }

  // Close the server socket
  close(serverSocket);

  return 0;
}
