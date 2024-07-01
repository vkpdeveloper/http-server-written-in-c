#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
  int socket_fd, reuse = 1, connection_backlog = 10;

  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  // Setting socket ops for address reuse (not port)
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) ==
      -1) {
    printf("Failed to set socket options: %s\n", strerror(errno));
    return 1;
  }

  // Binding the port with the created socket fd
  struct sockaddr_in serve_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(4221),
      .sin_addr = {htonl(INADDR_ANY)},
  };
  if (bind(socket_fd, (struct sockaddr *)&serve_addr, sizeof(serve_addr)) ==
      -1) {
    printf("Failed to bind the port: %s\n", strerror(errno));
    return 1;
  }

  struct sockaddr client_addr;

  if (listen(socket_fd, connection_backlog) == -1) {
    printf("Failed to start listing: %s\n", strerror(errno));
    return 1;
  }

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t addresslen = sizeof(&client_addr);
    int client_fd =
        accept(socket_fd, (struct sockaddr *)&client_addr, &addresslen);
    if (client_fd == -1) {
      close(socket_fd);
      printf("Accept failed: %s\n", strerror(errno));
      return 1;
    }

    printf("New client is connected\n");

    // Example: Sending a welcome message to the client
    const char *welcome_message = "Welcome to the server!\n";
    send(client_fd, welcome_message, strlen(welcome_message), 0);

    close(client_fd);
  }

  close(socket_fd);
}
