#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define log(I, ...) (printf("ursh: " #I ": " __VA_ARGS__))
#define err         ": %s\n", strerror(errno)

/* start a tcp server. returns fd or -1. */
int start_server (char *addr, int port);

/* handle clients */
int handle_clients (int fd);

/* serve for a client. returns -1 on any error */
int serve_for_client (int clfd);


static char *ursh_defargs[] = { "/bin/sh", "-i" };
static char **ursh_argv     = ursh_defargs;

static char *ursh_addr      = "0.0.0.0";
static int   ursh_port      = 3030;


void sigchld_handler(int signo) {
  pid_t who;
  while ((who = waitpid(-1, NULL, WNOHANG)) > 0)
    log(info, "disconnected: %d\n", who);
}


int main (int argc, char **argv)
{
  int fd;

  if (argc >= 2 && strcmp(argv[1], "help") == 0) {
    printf("usage: %s [addr] [port] [shell [args...]]\n", argv[0]);
    return -1;
  }

  if (argc >= 2)
    ursh_addr = argv[1];
  if (argc >= 3)
    ursh_port = atol(argv[2]);
  if (argc >= 4)
    ursh_argv = &argv[3];

  /* reap child procs */
  struct sigaction sa;
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  /* setup server */
  fd = start_server(ursh_addr, ursh_port);
  if (fd < 0)
    return -1;

  if (handle_clients(fd) < 0)
    return -1;

  close(fd);
  return 0;
}


int start_server (char *addr, int port)
{
  struct sockaddr_in in;
  int fd;

  in.sin_family = AF_INET;
  in.sin_port = htons(port);
  
  if (inet_pton(in.sin_family, addr, &in.sin_addr) <= 0) {
    log(error, "inet_pton()" err);
    return -1;
  }

  log(info, "make socket\n");
  fd = socket(in.sin_family, SOCK_STREAM, 0);
  if (fd < 0) {
    log(error, "socket()" err);
    return -1;
  }

  log(info, "bind socket\n");
  if (bind(fd, (struct sockaddr*)&in, sizeof(in)) < 0) {
    log(error, "bind()" err);
    close(fd);
    return -1;
  }

  log(info, "listen for connections\n");
  if (listen(fd, SOMAXCONN) < 0) {
    log(error, "listen()" err);
    close(fd);
    return -1;
  }

  log(info, "server listening to: %s:%d\n",
      inet_ntoa(in.sin_addr), ntohs(in.sin_port));
  return fd;
}


int handle_clients (int fd)
{
  for (;;) {
    int clfd;
    pid_t pid;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    clfd = accept(fd, (struct sockaddr *)&addr, &len);
    if (clfd < 0) {
      log(error, "accept()" err);
      /* should i? */
      /*return -1;*/
      continue;
    }

    log(info, "client: %s:%d\n",
        inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    pid = fork();
    if (pid < 0) {
      log(error, "fork()" err);
      close(clfd);
      continue;
    }

    /* child */
    if (pid == 0) {
      log(info, "fork pid: %d\n", getpid());
      close(fd);
      serve_for_client(clfd);
      close(clfd);
      _exit(0);
    }

    /* parent */
    close(clfd);
  }
  return 0;
}


int serve_for_client (int clfd)
{
  log(info, "redirecting stdio\n");
  dup2(clfd, STDIN_FILENO);
  dup2(clfd, STDOUT_FILENO);
  dup2(clfd, STDERR_FILENO);
  log(info, "hello from server!\n");
  log(info, "handler pid: %d\n", getpid());
  log(info, "starting shell...\n");

  return execvp(ursh_argv[0], ursh_argv);
}
