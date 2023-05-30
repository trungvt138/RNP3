#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#define SRV_ADDRESS "127.0.0.1"
#define SRV_PORT 7777

int main(int argc, char** argv) {
  if (argc != 3) {
    printf("Usage: %s <server-address> <server-port>\n", argv[0]);
    return 1;
  }

  const char* serverAddress = argv[1];
  int serverPort = atoi(argv[2]);

  int s_tcp;
  struct sockaddr_in sa;

  sa.sin_family = AF_INET;
  sa.sin_port = htons(serverPort);

  if (inet_pton(sa.sin_family, serverAddress, &sa.sin_addr.s_addr) <= 0) {
    perror("Address Conversion");
    return 1;
  }

  if ((s_tcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    perror("TCP Socket");
    return 1;
  }

  if (connect(s_tcp, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
    perror("Connect");
    return 1;
  }

  const char* commands[] = {"List", "Files", "Get data", "Put data"};

  for (int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    ssize_t n;
    if ((n = send(s_tcp, commands[i], strlen(commands[i]), 0)) > 0) {
      printf("Command '%s' sent (%zi bytes).\n", commands[i], n);
      send(s_tcp, "\x04", 1, 0);  // Send EOT character
    }
  }

  close(s_tcp);
  return 0;
}
