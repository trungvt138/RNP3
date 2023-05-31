#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <netdb.h>

#define MAX_COMMAND_LENGTH 256

int main(int argc, char** argv) {
  if (argc != 3) {
    printf("Usage: %s <server_address> <server_port>\n", argv[0]);
    return 1;
  }

  const char* serverAddress = argv[1];
  const char* serverPort = argv[2];

  struct addrinfo hints, *serverInfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(serverAddress, serverPort, &hints, &serverInfo) != 0) {
    perror("getaddrinfo");
    return 1;
  }

  int clientSocket = socket(serverInfo->ai_family, serverInfo->ai_socktype, serverInfo->ai_protocol);
  if (clientSocket < 0) {
    perror("Socket");
    return 1;
  }

  if (connect(clientSocket, serverInfo->ai_addr, serverInfo->ai_addrlen) < 0) {
    perror("Connect");
    return 1;
  }

  printf("Connected to server. Enter commands (List, Files, Get <filename>, Put <filename>, Quit):\n");

  char command[MAX_COMMAND_LENGTH];

  while (1) {
    printf("> ");
    fflush(stdout);

    if (fgets(command, sizeof(command), stdin) == NULL) {
      break;
    }

    // Remove trailing newline character
    command[strcspn(command, "\n")] = '\0';

    if (send(clientSocket, command, strlen(command), 0) < 0) {
      perror("Send");
      break;
    }

    if (strcmp(command, "Quit") == 0) {
      printf("Exiting...\n");
      break;
    }

    // Receive and display the server response
    char response[MAX_COMMAND_LENGTH];
    ssize_t bytesRead;

    if ((bytesRead = recv(clientSocket, response, sizeof(response) - 1, 0)) < 0) {
      perror("Receive");
      break;
    }

    if (bytesRead == 0) {
      printf("Server closed the connection\n");
      break;
    }

    response[bytesRead] = '\0';
    printf("Response: %s\n", response);
  }

  freeaddrinfo(serverInfo);
  close(clientSocket);
  return 0;
}