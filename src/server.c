#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define DEFAULT_PORT 0
#define MAX_CLIENTS 10

typedef struct {
  char hostname[256];
  int port;
} ClientInfo;

struct addrinfo {
    int ai_flags;           // Input flags
    int ai_family;          // Address family of the socket
    int ai_socktype;        // Socket type
    int ai_protocol;        // Protocol of the socket
    socklen_t ai_addrlen;   // Length of socket address
    struct sockaddr* ai_addr; // Socket address for the socket
    char* ai_canonname;     // Canonical name of the host
    struct addrinfo* ai_next; // Pointer to the next addrinfo structure
};

void handleListCommand(int clientSocket, int* clientSockets) {
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

  // Send the response back to the client
  send(clientSocket, response, strlen(response), 0);
}

void handleFilesCommand(int clientSocket) {
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

    // Append the filename and attributes to the response string
    snprintf(response + strlen(response), sizeof(response) - strlen(response), "%s\t%s\n", entry->d_name, fileAttributes);

    numFiles++;
  }

  // Append the total number of files to the response string
  snprintf(response + strlen(response), sizeof(response) - strlen(response), "Total Files: %d\n", numFiles);

  closedir(dir);

  // Send the response to the client
  if (send(clientSocket, response, strlen(response), 0) < 0) {
    perror("Send");
    return;
  }
}

void handleGetCommand(int clientSocket, const char* command) {
  // Parse the command to extract the filename and file attributes
  char filename[256];
  char fileAttributes[256];
  sscanf(command, "Get %s %[^\n]", filename, fileAttributes);

  // Open the file for reading
  FILE* file = fopen(filename, "r");
  if (file == NULL) {
    perror("File open");
    return;
  }

  // Read the file content and calculate the file size
  fseek(file, 0, SEEK_END);
  long fileSize = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Read the file content into a buffer
  char* fileContent = (char*)malloc(fileSize + 1);
  if (fileContent == NULL) {
    perror("Memory allocation");
    fclose(file);
    return;
  }
  fread(fileContent, 1, fileSize, file);
  fileContent[fileSize] = '\0';

  fclose(file);

  // Get the last modified time of the file
  struct stat fileStat;
  if (stat(filename, &fileStat) == -1) {
    perror("File stat");
    free(fileContent);
    return;
  }
  time_t lastModified = fileStat.st_mtime;

  // Prepare the response with the file content and attributes
  char response[4096];
  snprintf(response, sizeof(response), "Filename: %s\nLast Modified: %s\nSize: %ld bytes\n\n%s",
           filename, ctime(&lastModified), fileSize, fileContent);

  free(fileContent);

  // Send the response to the client
  if (send(clientSocket, response, strlen(response), 0) < 0) {
    perror("Send");
    return;
  }
}

void handlePutCommand(int clientSocket, const char* command) {
  // Extract the filename from the command
  const char* filename = command + 4;

  // Create a new file in the server directory
  FILE* file = fopen(filename, "w");
  if (file == NULL) {
    perror("File open");
    return;
  }

  // Set up the file descriptor set for select
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(clientSocket, &read_fds);

  // Set the timeout for select to 1 second
  struct timeval timeout;
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;

  // Receive and store the file data
  char buffer[1024];
  ssize_t bytesRead;
  while (select(clientSocket + 1, &read_fds, NULL, NULL, &timeout) > 0) {
    if ((bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
      fwrite(buffer, 1, bytesRead, file);
    }
  }

  fclose(file);

  // Get the server hostname and IP address
  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) < 0) {
    perror("gethostname");
    return;
  }

  struct addrinfo hints, *serverInfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hostname, NULL, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return;
  }

  char serverIP[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &((struct sockaddr_in*)serverInfo->ai_addr)->sin_addr, serverIP, sizeof(serverIP)) == NULL) {
    perror("inet_ntop");
    return;
  }

  freeaddrinfo(serverInfo);

  // Get the current date and time
  time_t rawTime;
  struct tm* timeInfo;
  time(&rawTime);
  timeInfo = localtime(&rawTime);
  if (timeInfo == NULL) {
    perror("localtime");
    return;
  }
  char datetime[64];
  strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", timeInfo);

  // Send the response to the client
  char response[256];
  snprintf(response, sizeof(response), "OK %s\n%s\n%s", hostname, serverIP, datetime);
  if (send(clientSocket, response, strlen(response), 0) < 0) {
    perror("Send");
    return;
  }
}

void handleCommand(int clientSocket, int* clientSockets, const char* command) {
  if (strcmp(command, "List") == 0) {
    handleListCommand(clientSocket, clientSockets);
  }
  else if (strcmp(command, "Files") == 0) {
    handleFilesCommand(clientSocket);
  }
  else if (strncmp(command, "Get", 3) == 0) {
    handleGetCommand(clientSocket, command);
  }
  else if (strncmp(command, "Put", 3) == 0) {
    handlePutCommand(clientSocket, command);
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
