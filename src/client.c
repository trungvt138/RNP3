#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_COMMAND_LENGTH 256
#define MAX_RESPONSE_LENGTH 4096

void responseToClientInChunk(int clientSocket, char* response) {
  // Send the response to the client in chunks
  const char EOT = 4;
  size_t responseLength = strlen(response);
  size_t sentBytes = 0;
  while (sentBytes < responseLength) {
    size_t chunkSize = responseLength - sentBytes;
    if (chunkSize > MAX_RESPONSE_LENGTH - 1) {
      chunkSize = MAX_RESPONSE_LENGTH - 1;
    }

    // Copy a chunk of the response to a temporary buffer
    char chunk[MAX_RESPONSE_LENGTH];
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

int main(int argc, char** argv) {
  if (argc != 3) {
    printf("Usage: %s <server_address> <server_port>\n", argv[0]);
    return 1;
  }

  const char* serverAddress = argv[1];
  const char* serverPort = argv[2];

  struct addrinfo hints, *serverInfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;  // Allow both IPv4 and IPv6
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(serverAddress, serverPort, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return 1;
  }

  int clientSocket = -1;
  struct addrinfo* p;
  for (p = serverInfo; p != NULL; p = p->ai_next) {
    clientSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (clientSocket < 0) {
      perror("Socket");
      continue;
    }

    if (connect(clientSocket, p->ai_addr, p->ai_addrlen) == 0) {
      // Connection successful
      break;
    }

    close(clientSocket);
    clientSocket = -1;
  }

  if (clientSocket < 0) {
    perror("Connect");
    freeaddrinfo(serverInfo);
    return 1;
  }

  char serverAddressStr[INET6_ADDRSTRLEN];
  void* serverIP = NULL;
  if (p->ai_family == AF_INET) {
    struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
    serverIP = &(ipv4->sin_addr);
  } else {
    struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
    serverIP = &(ipv6->sin6_addr);
  }

  // Convert the IP address to a human-readable format
  inet_ntop(p->ai_family, serverIP, serverAddressStr, sizeof(serverAddressStr));
  printf("Connected to server at %s. Enter commands (List, Files, Get <filename>, Put <filename>, Quit):\n", serverAddressStr);

  freeaddrinfo(serverInfo);

  char command[MAX_COMMAND_LENGTH];

  while (1) {
    printf("> ");
    fflush(stdout);

    if (fgets(command, sizeof(command), stdin) == NULL) {
      break;
    }

    // Remove trailing newline character
    command[strcspn(command, "\n")] = '\0';

    if (strcmp(command, "") == 0) {
      // Ignore empty command and continue to the next iteration
      continue;
    }

    if (send(clientSocket, command, strlen(command), 0) < 0) {
      perror("Send");
      break;
    }

    if (strcmp(command, "Quit") == 0) {
      printf("Exiting...\n");
      break;
    }

    // Receive and display the server response
    char response[MAX_RESPONSE_LENGTH];
    ssize_t bytesRead = 0;
    while (1) {
      ssize_t chunkBytesRead = recv(clientSocket, response + bytesRead, sizeof(response) - bytesRead - 1, 0);
      if (chunkBytesRead < 0) {
        perror("Receive");
        break;
      }

      bytesRead += chunkBytesRead;

      // Check if the end of the response has been reached
      if (response[bytesRead - 1] == 4) {
        response[bytesRead - 1] = '\0'; // Remove the EOT delimiter
        // Check if the response is an error message
        if (strcmp(response, "Invalid command") == 0) {
          printf("Invalid command. Please enter a valid command.\n");
          break;
        }
        break;
      }
    }

    if (bytesRead == 0) {
      printf("Server closed the connection\n");
      break;
    }

    printf("Response: %s\n", response);
  }

  close(clientSocket);
  return 0;
}
