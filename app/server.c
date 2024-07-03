#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define _POSIX_C_SOURCE 200809L

typedef struct {
  char *key;
  char *value;
} header;

typedef struct {
  char *method;
  float http_version;
  char *path;
  header **headers;
  char *body;
} http_request;

void reply(int client_fd, http_request *request);
char *extract_http_request_path(char *request_buffer);
char *extract_the_last_token(char *request_path);
void *parse_request(char *request_buffer, http_request *dst);
void *parse_request_line(char *line, http_request *dst);
header *parse_header(char *header_line, header *dst);
int sizeof_header(header **headers);
char *read_file(char *file_path);
void reply_with_404(int client_fd);

char *base_dir_path;

int main(int argc, char *argv[]) {
  setbuf(stderr, NULL);
  setbuf(stdout, NULL);

  base_dir_path = malloc(strlen(argv[2]));
  strcpy(base_dir_path, argv[2]);

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
      http_request *request = malloc(sizeof(http_request));
      if (request == NULL) {
        perror("malloc");
        return 1;
      }
      int *is_parsed = parse_request(buffer, request);
      printf("%s %s %f\n", request->method, request->path,
             request->http_version);
      if (is_parsed == NULL) {
        printf("failed to parse the request\n");
      } else {
        reply(client_fd, request);
      }
    }
    close(client_fd);
  }

  close(socket_fd);
}

void reply(int client_fd, http_request *request) {
  if (strcmp(request->path, "/") == 0) {
    const char *hello_world_message = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_fd, hello_world_message, strlen(hello_world_message), 0);
  } else if (strstr(request->path, "/echo/") != NULL) {
    char *copied_request_path = strdup(request->path);
    char *echo_message = extract_the_last_token((char *)copied_request_path);
    char *response_message = malloc(71 + 1 + strlen(echo_message));
    if (response_message == NULL) {
      return;
    }
    sprintf(response_message,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            "%d\r\n\r\n%s",
            (int)strlen(echo_message), echo_message);
    send(client_fd, response_message, strlen(response_message), 0);

    // freeing memory allocations
    free(response_message);
    free(copied_request_path);
  } else if (strstr(request->path, "/user-agent") != NULL) {
    header *f_header = NULL;
    int _current_count = 0;
    while (f_header == NULL) {
      header *_header = request->headers[_current_count];
      if (strstr(_header->key, "User-Agent") != NULL) {
        f_header = _header;
        break;
      }
      _current_count++;
    }
    char *response_message = malloc(1024);
    if (response_message == NULL) {
      perror("malloc");
      return;
    }
    int size_of_header_value = strlen(f_header->value);
    sprintf(response_message,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            "%d\r\n\r\n%s",
            size_of_header_value, f_header->value);
    send(client_fd, response_message, strlen(response_message), 0);
    free(response_message);
  } else if (strstr(request->path, "/files/") != NULL) {
    char *copied_request_path = strdup(request->path);

    char *file_path = extract_the_last_token((char *)copied_request_path);
    char *file_content = read_file(file_path);
    if (file_content == NULL) {
      reply_with_404(client_fd);
    } else {
      char *response_message = malloc(strlen(file_content) + 120);
      sprintf(response_message,
              "HTTP/1.1 200 OK\r\nContent-Type: "
              "application/octet-stream\r\nContent-Length: "
              "%ld\r\n\r\n%s",
              strlen(file_content), file_content);
      send(client_fd, response_message, strlen(response_message), 0);
      free(response_message);
    }
  } else {
    reply_with_404(client_fd);
  }
  return;
}

void reply_with_404(int client_fd) {
  const char *page_not_found_message = "HTTP/1.1 404 Not Found\r\n\r\n";
  send(client_fd, page_not_found_message, strlen(page_not_found_message), 0);
}

char *extract_http_request_path(char *buffer) {
  char *saveptr;
  char *method_path_version = strtok_r(buffer, "\r\n", &saveptr);
  char *request_path = strtok_r(method_path_version, " ", &saveptr);

  while (request_path != NULL) {
    request_path = strtok_r(NULL, " ", &saveptr);
    if (strstr(request_path, "/") != NULL) {
      break;
    }
  }

  return request_path;
}

char *extract_the_last_token(char *request_path) {
  char *saveptr, *next_token;
  char *final_token = strtok_r(request_path, "/", &saveptr);

  while ((next_token = strtok_r(NULL, "/", &saveptr)) != NULL) {
    final_token = next_token;
  }
  return final_token;
}

void *parse_request_line(char *line, http_request *dst) {
  char *copied_line = strdup(line);
  char *next_token, *saved_state;
  char *line_token = strtok_r(line, " ", &saved_state);
  if (line_token == NULL) {
    free(copied_line);
    return NULL;
  }
  // Copying the method of the request line to the http request struct and
  // making sure that it's null terminated
  dst->method = malloc(8);
  if (dst->method == NULL) {
    perror("malloc");
    free(copied_line); // Free the strdup'd memory if not needed
    return NULL;
  }
  strcpy(dst->method, line_token);
  while ((next_token = strtok_r(NULL, " ", &saved_state)) != NULL) {
    dst->path = malloc(strlen(next_token) + 1);
    strcpy(dst->path, next_token);
    dst->http_version = 1.1;
    break;
  }
  return dst;
}

header *parse_header(char *header_line, header *dst) {
  char *copied = strdup(header_line);

  char *saved_state;
  char *buffer_token = strtok_r(copied, ": ", &saved_state);

  dst->key = malloc(strlen(buffer_token) + 1);
  strcpy(dst->key, buffer_token);

  buffer_token = strtok_r(NULL, ": ", &saved_state);
  dst->value = malloc(strlen(buffer_token) + 1);
  strcpy(dst->value, buffer_token);

  free(copied);
  return dst;
}

void *parse_request(char *request_buffer, http_request *dst) {
  char *buffer = strdup(request_buffer);
  char *saved_state;
  char *next_token;
  char *buffer_token = strtok_r(buffer, "\r\n\r\n", &saved_state);

  // parsing the request line and passing the dst of http_request struct
  int *is_request_line_parsed = parse_request_line(buffer_token, dst);
  if (is_request_line_parsed == NULL) {
    return NULL;
  }
  dst->headers = malloc(sizeof(header));
  if (dst->headers == NULL) {
    return NULL;
  }
  int count_of_header = 0;
  while ((next_token = strtok_r(NULL, "\r\n\r\n", &saved_state)) != NULL) {
    if (saved_state == NULL) {
      // TODO: Parse the request body in this block
    } else {
      // Parse each header in this block
      header *_header = malloc(sizeof(header));
      parse_header(next_token, _header);
      dst->headers[count_of_header] = _header;
      count_of_header++;
      dst->headers =
          realloc(dst->headers, (count_of_header + 1) * sizeof(header));
      if (dst->headers == NULL) {
        return NULL;
      }
    }
    buffer_token = next_token;
  }

  // adding the final null terminated NULL to mark the end of the array
  dst->headers = realloc(dst->headers, (count_of_header + 1) * sizeof(header));
  dst->headers[count_of_header] = NULL;
  return dst;
}

int sizeof_header(header **headers) {
  int size = 0;
  while (headers[size] != NULL) {
    size++;
  }
  return size;
}

char *read_file(char *file_path) {
  char *abs_file_path = malloc(strlen(file_path) + 5);
  sprintf(abs_file_path, "%s%s", base_dir_path, file_path);
  printf("file path is: %s\n", abs_file_path);
  int content_size = 100;
  char *file_content = malloc(content_size);
  FILE *fp;
  fp = fopen(abs_file_path, "r");
  if (fp == NULL) {
    free(abs_file_path);
    free(file_content);
    return NULL;
  }

  fseek(fp, 0, SEEK_SET);

  while (fread(file_content, sizeof(char), content_size, fp) == 0) {
    content_size++;
    file_content = realloc(file_content, content_size);
  }

  fclose(fp);
  return file_content;
}
