#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define TIME_BUFFER_SIZE 1024
#define QUEUE_LENGTH 128
#define MAX_CHILDREN 100
#define on_error(...)                                                          \
  {                                                                            \
    fprintf(stderr, __VA_ARGS__);                                              \
    fflush(stderr);                                                            \
    exit(1);                                                                   \
  }

static int child_pids[MAX_CHILDREN];

static void log_connection(struct sockaddr_in *client) {
  time_t curr_time = time(NULL);
  struct tm *local_time = localtime(&curr_time);
  char time_buffer[TIME_BUFFER_SIZE];

  char addr_buffer[INET_ADDRSTRLEN];
  inet_ntop(client->sin_family, &client->sin_addr, addr_buffer,
            INET_ADDRSTRLEN);

  int ret = strftime(time_buffer, TIME_BUFFER_SIZE, "%D:%r", local_time);
  if (ret <= 0) {
    printf("Error: %d\n", errno);
    exit(1);
  }

  printf("[%s] %s:%d\n", time_buffer, addr_buffer, client->sin_port);
}

int main(int argc, char *argv[]) {
  if (argc < 2)
    on_error("Usage: %s [port]\n", argv[0]);

  int port = atoi(argv[1]);

  int server_fd, client_fd, err;
  struct sockaddr_in server, client;
  char buf[BUFFER_SIZE];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
    on_error("Could not create socket\n");

  server.sin_family = AF_INET;
  server.sin_port = htons(port);
  server.sin_addr.s_addr = htonl(INADDR_ANY);

  int reuse_addr_opt_val = 1;
  int reuse_port_opt_val = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr_opt_val,
             sizeof reuse_addr_opt_val);
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &reuse_port_opt_val,
             sizeof reuse_port_opt_val);

  err = bind(server_fd, (struct sockaddr *)&server, sizeof(server));
  if (err < 0)
    on_error("Could not bind socket\n");

  err = listen(server_fd, QUEUE_LENGTH);
  if (err < 0)
    on_error("Could not listen on socket\n");

  printf("Server is listening on %d\n", port);

  while (1) {
    for (int i = 0; i < MAX_CHILDREN; i++) {
      if (child_pids[i] != 0)
        wait(&child_pids[i]);
    }

    socklen_t client_len = sizeof(client);
    client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);

    if (client_fd < 0)
      on_error("Could not establish new connection\n");

    log_connection(&client);

    int pid = fork();

    if (pid > 0) {
      char place_found = 0;
      for (int i = 0; i < MAX_CHILDREN; i++) {
        if (child_pids[i] == 0) {
          child_pids[i] = pid;
          place_found = 1;
          break;
        }
      }
      if (!place_found) {
        on_error("No place for another child process\n");
      }
      // close file desc in parent to prevent table overload
      close(client_fd);
      continue;
    }
    if (pid < 0)
      on_error("Error while forking\n");

    /* We are in child here */

    while (1) {
      int read = recv(client_fd, buf, BUFFER_SIZE, 0);

      if (!read) {
        exit(0); // done reading
      }
      if (read < 0)
        on_error("Client read failed\n");

      err = send(client_fd, buf, read, 0);
      if (err < 0)
        on_error("Client write failed\n");
    }
  }

  return 0;
}
