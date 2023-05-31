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
const char EOT_CHAR = 4;

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
      char fileInfo[1024];
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
      sendStream(clientSocket, (char*)&EOT_CHAR, 1);
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

int main(int argc, char** argv) {
  if (argc > 2) {
    printf("Usage: %s [port]\n", argv[0]);
    return 1;
  }

  int serverPort = DEFAULT_PORT;
  if (argc == 2) {
    serverPort = atoi(argv[1]);
  }

  int s_tcp;
  struct sockaddr_in sa, sa_client;
  unsigned int sa_len = sizeof(struct sockaddr_in);
  char command[256];

  sa.sin_family = AF_INET;
  sa.sin_port = htons(serverPort);
  sa.sin_addr.s_addr = INADDR_ANY;

  if ((s_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("TCP Socket");
    return 1;
  }

  if (bind(s_tcp, (struct sockaddr*)&sa, sa_len) < 0) {
    perror("Bind");
    return 1;
  }

  if (serverPort == DEFAULT_PORT) {
    if (getsockname(s_tcp, (struct sockaddr*)&sa, &sa_len) < 0) {
      perror("Get socket name");
      return 1;
    }
    printf("Server port assigned by the operating system: %d\n", ntohs(sa.sin_port));
  }

  if (listen(s_tcp, 5) < 0) {
    perror("Listen");
    close(s_tcp);
    return 1;
  }

  printf("Waiting for TCP connections...\n");

  fd_set master;
  fd_set read_fds;
  int fdmax;

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  FD_SET(s_tcp, &master);
  fdmax = s_tcp;

  int clientSockets[MAX_CLIENTS];
  for (int i = 0; i < MAX_CLIENTS; i++) {
    clientSockets[i] = -1;
  }

  while (1) {
    read_fds = master;

    if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
        perror("Select");
        return 1;
    }

    if (FD_ISSET(s_tcp, &read_fds)) {
      int newSocket;
      if ((newSocket = accept(s_tcp, (struct sockaddr*)&sa_client, &sa_len)) < 0) {
          perror("Accept");
          close(s_tcp);
          return 1;
      }

      // Add the new client socket to the array
      int i;
      for (i = 0; i < MAX_CLIENTS; i++) {
        if (clientSockets[i] == -1) {
            clientSockets[i] = newSocket;
            break;
        }
      }

      FD_SET(newSocket, &master);
      if (newSocket > fdmax) {
        fdmax = newSocket;
      }

      printf("New connection established\n");
      printf("%d\n", fdmax);
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clientSockets[i] != -1 && FD_ISSET(clientSockets[i], &read_fds)) {
        ssize_t n;
        if ((n = recv(clientSockets[i], command, sizeof(command) - 1, 0)) > 0) {
          command[n] = '\0';
          printf("Command received: %s\n", command);
          handleCommand(clientSockets[i], clientSockets, command);
        } else if (n == 0) {
          printf("Client disconnected\n");
          close(clientSockets[i]);
          FD_CLR(clientSockets[i], &master);
          clientSockets[i] = -1;
        } else {
          perror("Receive");
        }
      }
    }
  }

  close(s_tcp);
  return 0;
}
