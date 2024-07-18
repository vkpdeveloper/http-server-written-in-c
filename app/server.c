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

struct header {
  char *key;
  char *value;
};

typedef struct {
  char *method;
  float http_version;
  char *path;
  struct header **headers;
  char *body;
} http_request;

void reply(int client_fd, http_request *request);
char *extract_http_request_path(char *request_buffer);
char *extract_the_last_token(char *request_path);
void *parse_request(char *request_buffer, http_request *dst);
void *parse_request_line(char *line, http_request *dst);
struct header *parse_headers(char *header_line, struct header *dst);
int sizeof_header(struct header **headers);
char *read_file(char *filename);
int write_file(char *filename, char *content);
void reply_with_404(int client_fd);
char *parse_client_content_encoding_headers(struct header **headers);
char *get_header(struct header **headers, const char *header_name);

char *base_dir_path;

int main(int argc, char *argv[]) {
  setbuf(stderr, NULL);
  setbuf(stdout, NULL);

  if (argc >= 2) {
    base_dir_path = malloc(strlen(argv[2]));
    strcpy(base_dir_path, argv[2]);
  } else {
    base_dir_path = malloc(7);
    strcpy(base_dir_path, "/tmp/");
  }

  printf("Setting --directory path to: %s\n", base_dir_path);

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
    http_request *request;

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    if ((bytes_received = recv(client_fd, &buffer, BUFFER_SIZE, 0)) > 0) {
      buffer[bytes_received] = '\0';
    }

    if (bytes_received == -1) {
      printf("Client disconnected\n");
    } else {
      request = malloc(sizeof(http_request));
      if (request == NULL) {
        perror("malloc");
        return 1;
      }
      int *is_parsed = parse_request(buffer, request);
      if (is_parsed == NULL) {
        printf("failed to parse the request\n");
      } else {
        printf("%s %s %f\n", request->method, request->path,
               request->http_version);
        reply(client_fd, request);
        free(request);
      }
    }
    close(client_fd);
  }
  close(socket_fd);
}

void reply(int client_fd, http_request *request) {
  char *response_message = NULL;
  if (strcmp(request->path, "/") == 0) {
    const char *hello_world_message = "HTTP/1.1 200 OK\r\n\r\n";
    send(client_fd, hello_world_message, strlen(hello_world_message), 0);
  } else if (strstr(request->path, "/echo/") != NULL) {
    char *echo_message = extract_the_last_token(request->path);
    char *content_encoding_algorithms =
        get_header(request->headers, "Accept-Encoding");
    if (content_encoding_algorithms == NULL) {
      response_message = malloc(71 + 1 + strlen(echo_message));
      if (response_message == NULL) {
        return;
      }
      sprintf(response_message,
              "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
              "%d\r\n\r\n%s",
              (int)strlen(echo_message), echo_message);
    } else {
      if ((strstr(content_encoding_algorithms, "gzip") != NULL)) {
        response_message = malloc(100 + 1 + strlen(echo_message));
        if (response_message == NULL) {
          return;
        }
        sprintf(response_message,
                "HTTP/1.1 200 OK\r\nContent-Type: "
                "text/plain\r\nContent-Encoding: gzip\r\nContent-Length: "
                "%d\r\n\r\n%s",
                (int)strlen(echo_message), echo_message);
      } else {
        response_message = malloc(71 + 1 + strlen(echo_message));
        if (response_message == NULL) {
          return;
        }
        sprintf(
            response_message,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            "%d\r\n\r\n%s",
            (int)strlen(echo_message), echo_message);
      }
    }
    send(client_fd, response_message, strlen(response_message), 0);
  } else if (strstr(request->path, "/user-agent") != NULL) {
    char *user_agent = get_header(request->headers, "User-Agent");
    if (user_agent == NULL) {
      return;
    }
    response_message = malloc(1024);
    if (response_message == NULL) {
      perror("malloc");
      return;
    }
    int size_of_header_value = strlen(user_agent);
    sprintf(response_message,
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            "%d\r\n\r\n%s",
            size_of_header_value, user_agent);
    send(client_fd, response_message, strlen(response_message), 0);
    free(user_agent);
  } else if (strstr(request->method, "GET") != NULL &&
             strstr(request->path, "/files/") != NULL) {
    char *file_path = extract_the_last_token(request->path);
    char *file_content = read_file(file_path);
    if (file_content == NULL) {
      reply_with_404(client_fd);
    } else {
      response_message = malloc(strlen(file_content) + 120);
      if (response_message == NULL) {
        perror("malloc");
        free(file_content);
        free(file_path);
        return;
      }
      sprintf(response_message,
              "HTTP/1.1 200 OK\r\nContent-Type: "
              "application/octet-stream\r\nContent-Length: "
              "%ld\r\n\r\n%s",
              strlen(file_content), file_content);
      send(client_fd, response_message, strlen(response_message), 0);
    }
    free(file_content);
    // free(file_path);
  } else if (strstr(request->method, "POST") != NULL &&
             strstr(request->path, "/files/") != NULL) {
    char *file_path = extract_the_last_token(request->path);

    if (write_file(file_path, request->body) == 0) {
      reply_with_404(client_fd);
    } else {
      const char *created_response = "HTTP/1.1 201 Created\r\n\r\n";
      send(client_fd, created_response, strlen(created_response), 0);
    }
  } else {
    reply_with_404(client_fd);
  }
  if (response_message != NULL) {
    free(response_message);
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
  char *copied_request_path = strdup(request_path);
  char *saveptr, *next_token;
  char *final_token = strtok_r(copied_request_path, "/", &saveptr);

  while ((next_token = strtok_r(NULL, "/", &saveptr)) != NULL) {
    final_token = next_token;
  }
  free(next_token);
  free(saveptr);
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

struct header *parse_headers(char *header_line, struct header *dst) {
  char *copied = strdup(header_line);

  char *saved_state;
  char *buffer_token = strtok_r(copied, ": ", &saved_state);

  dst->key = malloc(strlen(buffer_token) + 1);
  strcpy(dst->key, buffer_token);

  char *header_value;
  buffer_token = strtok_r(NULL, ": ", &saved_state);
  header_value = malloc(strlen(buffer_token));
  strcpy(header_value, buffer_token);

  while (saved_state != NULL) {
    buffer_token = strtok_r(NULL, ": ", &saved_state);
    header_value =
        realloc(header_value, strlen(header_value) + strlen(buffer_token));
    strcat(header_value, buffer_token);
  }

  dst->value = malloc(strlen(header_value) + 1);
  strcpy(dst->value, header_value);

  free(copied);
  free(header_value);
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
  dst->headers = malloc(sizeof(struct header));
  if (dst->headers == NULL) {
    return NULL;
  }
  int count_of_header = 0;
  while ((next_token = strtok_r(NULL, "\r\n\r\n", &saved_state)) != NULL) {
    if (saved_state == NULL) {
      // TODO: Parse the request body in this block
      dst->body = malloc(strlen(next_token));
      strcpy(dst->body, next_token);
    } else {
      // Parse each header in this block
      struct header *_header = malloc(sizeof(struct header));
      parse_headers(next_token, _header);
      dst->headers[count_of_header] = _header;
      count_of_header++;
      dst->headers =
          realloc(dst->headers, (count_of_header + 1) * sizeof(struct header));
      if (dst->headers == NULL) {
        return NULL;
      }
    }
    buffer_token = next_token;
  }

  // adding the final null terminated NULL to mark the end of the array
  dst->headers =
      realloc(dst->headers, (count_of_header + 1) * sizeof(struct header));
  dst->headers[count_of_header] = NULL;

  return dst;
}

int sizeof_header(struct header **headers) {
  int size = 0;
  while (headers[size] != NULL) {
    size++;
  }
  return size;
}

char *read_file(char *filename) {
  char *copied_base_dir = strdup(base_dir_path);
  char *abs_file_path = strcat(copied_base_dir, filename);
  if (access(abs_file_path, F_OK) != 0) {
    return NULL;
  }
  FILE *fp = fopen(abs_file_path, "r");
  if (fp == NULL) {
    printf("fp failed: %u %s\n", errno, strerror(errno));
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  char *file_content = malloc(size);
  fseek(fp, 0, SEEK_SET);

  fread(file_content, sizeof(char), size, fp);

  fclose(fp);
  free(copied_base_dir);
  return file_content;
}

int write_file(char *filename, char *content) {
  FILE *fp = fopen(strcat(base_dir_path, filename), "w");
  if (fp == NULL) {
    return 0;
  }
  fprintf(fp, "%s", content);
  fclose(fp);
  return 1;
}

char *parse_client_content_encoding_headers(struct header **headers) {
  // int headers_size = sizeof_header(headers);
  // char *algorithms = NULL;
  // char *encoding_algorithms;
  // for (int i = 0; i < headers_size; i++) {
  //   header *_header = headers[i];
  //   if (strstr(_header->key, "Accept-Encoding") != NULL) {
  //     algorithms = malloc(strlen(_header->value));
  //     strcpy(algorithms, _header->value);
  //   }
  // }
  // return algorithms;
  return "";
}

char *get_header(struct header **headers, const char *header_name) {
  int headers_size = sizeof_header(headers);
  char *value;
  for (int i = 0; i < headers_size; i++) {
    struct header *_header = headers[i];
    if (strstr(_header->key, header_name) != NULL) {
      value = malloc(strlen(_header->value));
      strcpy(value, _header->value);
      break;
    }
  }
  return value;
}
