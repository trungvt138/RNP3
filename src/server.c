#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>

#define DEFAULT_PORT 0

void handleCommand(int clientSocket, const char* command) {
  // Code to handle commands from clients
  // ...
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

      FD_SET(newSocket, &master);
      if (newSocket > fdmax) {
        fdmax = newSocket;
      }

      printf("New connection established\n");
    }

    for (int i = s_tcp + 1; i <= fdmax; i++) {
      if (FD_ISSET(i, &read_fds)) {
        ssize_t n;
        if ((n = recv(i, command, sizeof(command) - 1, 0)) > 0) {
          command[n] = '\0';
          printf("Command received: %s\n", command);

          if (command[n - 1] == 4) {
            handleCommand(i, command);
          }
        } else if (n == 0) {
          printf("Client disconnected\n");
          close(i);
          FD_CLR(i, &master);
        } else {
          perror("Receive");
        }
      }
    }
  }

  close(s_tcp);
  return 0;
}
