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
#include <netdb.h>

#define DEFAULT_PORT 0
#define CHUNK_SIZE 1024
#define MAX_RESPONSE_LENGTH 4096
#define MAX_COMMAND_LENGTH 256
#define MAX_CLIENTS 10

/**
 * Store client hostname and port number.
*/
typedef struct {
  char hostname[256];
  int port;
} ClientInfo;

struct ServerInfo {
  char hostname[256];
  char serverIP[INET6_ADDRSTRLEN];
};/**
 * @brief Sends a response to the client in chunks. It splits the response
 * into smaller chunks and sends them to the client.
 * 
 * @param clientSocket The client socket which receives the response.
 * @param response The response message to be sent to the client.
 * @return void.
 * 
*/
void responseToClientInChunk(int clientSocket, const char* response) {
  // Send the response to the client in chunks
  const char EOT = 4;
  size_t responseLength = strlen(response);
  size_t sentBytes = 0;

  // Allocate the chunk buffer
  char chunk[MAX_RESPONSE_LENGTH];

  while (sentBytes < responseLength) {
    size_t chunkSize = responseLength - sentBytes;
    if (chunkSize > MAX_RESPONSE_LENGTH - 1) {
      chunkSize = MAX_RESPONSE_LENGTH - 1;
    }

    // Copy a chunk of the response to the chunk buffer
    memcpy(chunk, response + sentBytes, chunkSize);
    chunk[chunkSize] = '\0';

    // Send the chunk to the client
    if (send(clientSocket, chunk, strlen(chunk), 0) < 0) {
      perror("Send");
      break;
    }

    sentBytes += chunkSize;
  }

  // Send the EOT delimiter to mark the end of the response
  if (send(clientSocket, &EOT, sizeof(EOT), 0) < 0) {
    perror("Send");
  }
}

void getServerInfo(struct ServerInfo* info) {
  if (gethostname(info->hostname, sizeof(info->hostname)) < 0) {
    perror("gethostname");
    return;
  }

  struct addrinfo hints, *serverInfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(info->hostname, NULL, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return;
  }

  void* serverAddr;
  if (serverInfo->ai_family == AF_INET) {
    // IPv4
    struct sockaddr_in* ipv4 = (struct sockaddr_in*)serverInfo->ai_addr;
    serverAddr = &(ipv4->sin_addr);
    if (inet_ntop(AF_INET, serverAddr, info->serverIP, sizeof(info->serverIP)) == NULL) {
      perror("inet_ntop");
      freeaddrinfo(serverInfo);
      return;
    }
  } else {
    // IPv6
    struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)serverInfo->ai_addr;
    serverAddr = &(ipv6->sin6_addr);
    if (inet_ntop(AF_INET6, serverAddr, info->serverIP, sizeof(info->serverIP)) == NULL) {
      perror("inet_ntop");
      freeaddrinfo(serverInfo);
      return;
    }
  }

  freeaddrinfo(serverInfo);
}

/**
 * @brief etrieves the client information of all connected sockets and 
 * sends a response to the client with the list of connected clients.
 * 
 * @param clientSocket The client socket which receives the response.
 * @param clientsSockets The array of the connected client sockets.
 * @return void
*/
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
  char response[MAX_RESPONSE_LENGTH];
  sprintf(response, "Connected Clients:\n");
  for (int i = 0; i < numClients; i++) {
      char clientInfo[256];
      sprintf(clientInfo, "%s:%d\n", clients[i].hostname, clients[i].port);
      strcat(response, clientInfo);
  }
  sprintf(response + strlen(response), "Total Clients: %d", numClients);

  // Send the response to the client in chunks
  responseToClientInChunk(clientSocket, response);
}

/**
 * @brief retrieves the list of files in the server directory and 
 * sends a response to the client with the file names and their attributes.
 * 
 * @param clientSocket The client socket which receives the response.
 * @return void.
*/
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

  // Send the response to the client in chunks
  responseToClientInChunk(clientSocket, response);
}

/**
 * @brief handles the "Get" command from the client. It extracts the 
 * filename from the command, reads the file content, and sends a response 
 * to the client with the file information.
 * 
 * @param clientSocket The client socket which receives the response.
 * @return void.
*/
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

  size_t bytesRead = fread(fileContent, 1, fileSize, file);
  if (bytesRead != (size_t)fileSize) {
      perror("fread");
      return;// Handle the error or exit the function
  }

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

  // Send the response to the client in chunks
  responseToClientInChunk(clientSocket, response);
}

/**
 * @brief handles the "Put" command from the client. It receives the file 
 * data from the client and writes it to a file in the server directory. 
 * After receiving the file, it sends a response to the client with the 
 * server hostname, IP address, and current date and time.
 * 
 * @param clientSocket The client socket which receives the response.
 * @return void.
*/
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
  struct ServerInfo serverInfo;
  getServerInfo(&serverInfo);

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

  // Send the response to the client in chunks
  char response[MAX_RESPONSE_LENGTH];
  snprintf(response, sizeof(response), "OK %s\n%s\n%s", hostname, serverIP, datetime);
  responseToClientInChunk(clientSocket, response);
}

/**
 * @brief The function handleCommand is called to handle the client command based on its type. 
 * It dispatches the command to the appropriate handler function.
 * 
 * @param clientSocket the client socket which receives the response.
 * @param clientSockets the list of the connected client sockets.
 * @param response the response message to be sent to the client.
 * @return void.
*/
void handleCommand(int clientSocket, int* clientSockets, const char* command, fd_set* master) {
  if (strcmp(command, "List") == 0) {
    handleListCommand(clientSocket, clientSockets);
  }
  else if (strncmp(command, "Files", 5) == 0) {
    handleFilesCommand(clientSocket);
  }
  else if (strncmp(command, "Get", 3) == 0) {
    handleGetCommand(clientSocket, command);
  }
  else if (strncmp(command, "Put", 3) == 0) {
    handlePutCommand(clientSocket, command);
  }
  else if (strncmp(command, "Quit", 4) == 0) {
    // Client requested to quit, close the connection
    printf("Client requested to quit. Closing connection.\n");
    close(clientSocket);
    FD_CLR(clientSocket, master);
    // Remove the client socket from the array
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (clientSockets[i] == clientSocket) {
        clientSockets[i] = -1;
        break;
      }
    }
  }
  else {
    // Invalid command received, force the client to send the right command
    const char* response = "Invalid command. Please send a valid command (List, Files, Get <filename>, Put <filename>)";
    responseToClientInChunk(clientSocket, response);
  }
}

int main(int argc, char** argv) {
  // the address and server port is passed as a command-line argument and stored in 
  // the address and port variable.
  if (argc != 3) {
      printf("Usage: %s [address] [port]\n", argv[0]);
      return 1;
  }

  const char* address = argv[1];
  const char* port = argv[2];

  // The socket is created and bound to the specified port using the 
  // getaddrinfo, socket, and bind functions.
  int s_tcp;
  struct sockaddr_storage sa, sa_client;
  socklen_t sa_len = sizeof(struct sockaddr_storage);
  char command[256];

  struct addrinfo hints, *serverInfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // Allow both IPv4 and IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // Use the local IP address

  if (getaddrinfo(address, port, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return 1;
  }

  s_tcp = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
  if (s_tcp < 0) {
    perror("TCP Socket");
    freeaddrinfo(serverInfo);
    return 1;
  }

  if (bind(s_tcp, serverInfo->ai_addr, serverInfo->ai_addrlen) < 0) {
    perror("Bind");
    freeaddrinfo(serverInfo);
    return 1;
  }

  freeaddrinfo(serverInfo);

  if (getsockname(s_tcp, (struct sockaddr*)&sa, &sa_len) < 0) {
    perror("Get socket name");
    close(s_tcp);
    return 1;
  }

  if (sa.ss_family == AF_INET) {
    struct sockaddr_in* ipv4 = (struct sockaddr_in*)&sa;
    printf("Server port assigned by the operating system: %d\n", ntohs(ipv4->sin_port));
  } else {
    struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)&sa;
    printf("Server port assigned by the operating system: %d\n", ntohs(ipv6->sin6_port));
  }

  // The server starts listening for TCP connections using the listen function.
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

    // The select function is used to monitor the server socket 
    // and client sockets for incoming data or connections
    if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1) {
      perror("Select");
      return 1;
    }

    // When a new connection is established, a new client socket is
    // created, added to the clientSockets array, and added to the master set.
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

      // Add the new socket to the master set
      FD_SET(newSocket, &master);

      // Update the maximum file descriptor
      if (newSocket > fdmax) {
        fdmax = newSocket;
      }

      printf("New connection established\n");
    }

    // The server continues to loop and handle client commands until it is terminated.
    for (int i = 0; i < MAX_CLIENTS; i++) {
      // If activity is detected on a client socket,
      if (clientSockets[i] != -1 && FD_ISSET(clientSockets[i], &read_fds)) {
        ssize_t n;

        // The received command is read using the recv function.
        if ((n = recv(clientSockets[i], command, sizeof(command) - 1, 0)) > 0) {
          command[n] = '\0';
          printf("Received command from client: %s\n", command);

          // The command is then passed to the handleCommand function for processing.
          handleCommand(clientSockets[i], clientSockets, command, &master);
        } else if (n == 0) {
          // Connection closed by the client
          printf("Client closed the connection\n");
          close(clientSockets[i]);
          FD_CLR(clientSockets[i], &master);
          clientSockets[i] = -1;
        } else {
          perror("Receive");
          close(clientSockets[i]);
          FD_CLR(clientSockets[i], &master);
          clientSockets[i] = -1;
        }
      }
    }
  }

  close(s_tcp);
  return 0;
}