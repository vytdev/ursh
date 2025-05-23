#define _XOPEN_SOURCE 600
#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__has_include) && __has_include(<pty.h>)
#  include <pty.h>   /* posix_openpt() */
#endif

#define log(I, ...) (printf("ursh: " #I ": " __VA_ARGS__))
#define err         ": %s\n", strerror(errno)

/* start a tcp server. returns fd or -1. */
int start_server (char *addr, int port);

/* handle clients */
int handle_clients (int fd);

/* serve for a client. returns -1 on any error */
int serve_for_client (int clfd);

/* redirect what fd1 sends to fd2, and fd2 to fd1 */
int proxy_loop (int fd1, int fd2);


static char *ursh_defargs[] = { "/bin/sh", "-i" };
static char **ursh_argv     = ursh_defargs;

static char *ursh_addr      = "0.0.0.0";
static int   ursh_port      = 3030;


void sigchld_handler(int signo) {
  pid_t who;
  while ((who = waitpid(-1, NULL, WNOHANG)) > 0)
    log(info, "exited: pid %d\n", who);
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
  int opt;

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

  log(info, "set socket opts\n");
  opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    log(error, "setsockopt()" err);
    close(fd);
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
      close(fd);
      serve_for_client(clfd);
      /* no return */
    }

    /* parent */
    close(clfd);
  }
  return 0;
}


int serve_for_client (int clfd)
{
  int master_fd, slave_fd;
  char *slave_name;
  pid_t pid;

  /* master fd is where the socket and the pty communicate. */
  master_fd = posix_openpt(O_RDWR | O_NOCTTY);
  grantpt(master_fd);
  unlockpt(master_fd);
  slave_name = ptsname(master_fd);
  slave_fd = open(slave_name, O_RDWR);

  pid = fork();
  if (pid < 0) {
    log(error, "fork()" err);
    close(clfd);
    close(master_fd);
    close(slave_fd);
    _exit(-1);
  }

  /* parent (for the proxy loop) */
  if (pid > 0) {
    log(info, "fork pid: %d (proxy)\n", getpid());
    close(slave_fd);
    proxy_loop(clfd, master_fd);
    close(master_fd);
    close(clfd);
    waitpid(pid, NULL, 0);
    _exit(0);
  }

  /* child (the shell) */
  log(info, "fork pid: %d (shell)\n", getpid());

  close(master_fd);
  close(clfd);
  setsid();
  ioctl(slave_fd, TIOCSCTTY, 0);

  log(info, "redirecting stdio\n");
  dup2(slave_fd, STDIN_FILENO);
  dup2(slave_fd, STDOUT_FILENO);
  dup2(slave_fd, STDERR_FILENO);
  if (slave_fd > STDERR_FILENO)
    close(slave_fd);

  log(info, "starting shell...\n");
  log(info, "shell pid: %d\n", getpid());

  execvp(ursh_argv[0], ursh_argv);
  log(error, "execvp()" err);
  _exit(-1);
}


int proxy_loop (int fd1, int fd2)
{
  ssize_t len;
  struct pollfd fds[2];
  char buf[4096];

  fds[0].fd = fd1;
  fds[0].events = POLLIN;
  fds[1].fd = fd2;
  fds[1].events = POLLIN;

  for (;;) {
    if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1) < 0) {
      log(error, "proxy: poll()" err);
      return -1;
    }

    if (fds[0].revents & (POLLIN | POLLHUP)) {
      len = read(fd1, buf, sizeof(buf));
      if (len <= 0)
        return 0;
      write(fd2, buf, len);
    }

    if (fds[1].revents & (POLLIN | POLLHUP)) {
      len = read(fd2, buf, sizeof(buf));
      if (len <= 0)
        return 0;
      write(fd1, buf, len);
    }
  }
}
