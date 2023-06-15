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

int main(int argc, char** argv) {
  // Check the command-line arguments
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

  // Retrieve the server's address information
  if (getaddrinfo(serverAddress, serverPort, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return 1;
  }

  int clientSocket = -1;
  struct addrinfo* p;
  // Iterate through all the available address information
  for (p = serverInfo; p != NULL; p = p->ai_next) {
    // Create a socket
    clientSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (clientSocket < 0) {
      perror("Socket");
      continue;
    }

    // Connect to the server
    if (connect(clientSocket, p->ai_addr, p->ai_addrlen) == 0) {
      // Connection successful
      break;
    }

    // Close the socket and try the next address
    close(clientSocket);
    clientSocket = -1;
  }

  // Check if the connection was established
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

  // Free the server address information
  freeaddrinfo(serverInfo);

  char command[MAX_COMMAND_LENGTH];
  char response[MAX_RESPONSE_LENGTH];
  ssize_t bytesRead = 0;
  int responseComplete = 0;

  // Create a set of file descriptors for select
  fd_set readfds;
  int maxfd;

  while (1) {
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    FD_SET(clientSocket, &readfds);
    maxfd = (STDIN_FILENO > clientSocket) ? STDIN_FILENO : clientSocket;

    printf("> ");
    fflush(stdout);

    // Wait for input from either stdin or the server
    if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
      perror("Select");
      break;
    }

    // Check if there is input from stdin
    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      if (fgets(command, sizeof(command), stdin) == NULL) {
        break;
      }

      // Remove trailing newline character
      command[strcspn(command, "\n")] = '\0';

      // Ignore empty command and continue to the next iteration
      if (strcmp(command, "") == 0) {
        continue;
      }

      // Send the command to the server
      if (send(clientSocket, command, strlen(command), 0) < 0) {
        perror("Send");
        break;
      }

      responseComplete = 0; // Reset the response completion flag

      // Check if the command is "Quit" to disconnect from the server
      if (strcmp(command, "Quit") == 0) {
        printf("Disconnecting from the server.\n");
        break;
      }
    }

    // Check if there is input from the server
    if (FD_ISSET(clientSocket, &readfds)) {
      bytesRead = recv(clientSocket, response, sizeof(response) - 1, 0);
      if (bytesRead < 0) {
        perror("Receive");
        break;
      } else if (bytesRead == 0) {
        // Server has closed the connection
        printf("Connection closed by the server.\n");
        break;
      }

      // Null-terminate the received data
      response[bytesRead] = '\0';

      // Print the received response
      printf("Response: %s\n", response);

      responseComplete = 1; // Set the response completion flag
    }

    // Check if a complete response is available
    if (responseComplete) {
      // Reset bytesRead and response buffer for the next response
      bytesRead = 0;
      memset(response, 0, sizeof(response));
      responseComplete = 0; // Reset the response completion flag
    }
  }

  // Close the client socket
  close(clientSocket);
  return 0;
}
