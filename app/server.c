#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

void reply_with_path_file(int client_fd, char *request_path);
char *extract_http_request_path(char *request_buffer);

int main() {
  setbuf(stderr, NULL);
  setbuf(stdout, NULL);

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

  if (listen(socket_fd, connection_backlog) == -1) {
    printf("Failed to start listing: %s\n", strerror(errno));
    return 1;
  }

  printf("Waiting for a client to connect...\n");

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

    printf("Client connected\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    if ((bytes_received = recv(client_fd, &buffer, BUFFER_SIZE, 0)) > 0) {
      buffer[bytes_received] = '\0';
    }

    if (bytes_received == -1) {
      printf("Client disconnected\n");
    } else {
      char *request_path = extract_http_request_path(buffer);
      reply_with_path_file(client_fd, request_path);
      close(client_fd);
    }

    close(client_fd);
  }

  close(socket_fd);
}

void reply_with_path_file(int client_fd, char *request_path) {
  if (strcmp(request_path, "/") == 0) {
    const char *hello_world_message = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_fd, hello_world_message, strlen(hello_world_message), 0);
  } else {
    const char *page_not_found_message = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(client_fd, page_not_found_message, strlen(page_not_found_message), 0);
  }
  return;
}

char *extract_http_request_path(char *buffer) {
  char *method_path_version = strtok(buffer, "\r\n");
  char *request_path = strtok(method_path_version, " ");

  while (request_path != NULL) {
    request_path = strtok(NULL, " ");
    if (strstr(request_path, "/") != NULL) {
      break;
    }
  }

  return request_path;
}
