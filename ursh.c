#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include "common.h"


#define log(I, ...) (printf("ursh: " #I ": " __VA_ARGS__))
#define err         ": %s\n", strerror(errno)

/* restore orig termios opts */
void disable_raw_mode (void);

/* disables line buffering and echo */
void enable_raw_mode (void);

/* connect to server. */
int connect_server (char *addr, int port);

/* connect to std fds. */
int connect_to_std (int fd);


static char *ursh_addr = "127.0.0.1";
static int   ursh_port = 3030;
static struct termios  orig_termios;


int main (int argc, char **argv)
{
  int fd;

  if (argc >= 2 && strcmp(argv[1], "help") == 0) {
    printf("usage: %s [addr] [port]\n"
           "\n"
           "URSH:   Userspace Remote Shell client.\n"
           "GitHub: https://github.com/vytdev/ursh\n"
           "\n"
           "Copyright (C) 2025 Vincent Yanzee J. Tan\n"
           "This program is distributed under the MIT License\n",
      argv[0]);
    return -1;
  }

  if (argc >= 2)
    ursh_addr = argv[1];
  if (argc >= 3)
    ursh_port = atol(argv[2]);

  fd = connect_server(ursh_addr, ursh_port);
  if (fd < 0)
    return -1;

  /* send input instantly */
  enable_raw_mode();
  atexit(disable_raw_mode);

  /* connect with the interactive shell */
  if (connect_to_std(fd) < 0)
    return -1;

  close(fd);
  return 0;
}


void disable_raw_mode (void)
{
  log(info, "disable raw mode\n");
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


void enable_raw_mode (void)
{
  log(info, "enable raw mode\n");
  struct termios raw;
  tcgetattr(STDIN_FILENO, &orig_termios);
  raw = orig_termios;
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int connect_server (char *addr, int port)
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

  log(info, "try connect\n");
  if (connect(fd, (struct sockaddr*)&in, sizeof(in)) < 0) {
    log(error, "connect()" err);
    close(fd);
    return -1;
  }

  log(info, "connected!\n");
  return fd;
}


int connect_to_std (int fd)
{
  ssize_t len;
  char buf[4096];
  struct pollfd fds[2];

  /* we can't just read()/write() directly. */
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[1].fd = fd;
  fds[1].events = POLLIN;

  for (;;) {
    if (poll(fds, sizeof(fds)/sizeof(struct pollfd), -1) < 0) {
      log(error, "poll()" err);
      return -1;
    }

    /* from stdin, as the user types */
    if (fds[0].revents & (POLLIN | POLLHUP)) {
      len = read(STDIN_FILENO, buf, sizeof(buf));
      if (len < 0) {
        log(error, "read()" err);
        return -1;
      }
      if (len == 0) {
        log(warn, "stdin closed\n");
        return -1;
      }
      if (write_all(fd, buf, len) != len) {
        log(error, "write_all()" err);
        return -1;
      }
    }

    /* from the socket */
    if (fds[1].revents & (POLLIN | POLLHUP)) {
      len = read(fd, buf, sizeof(buf));
      if (len < 0) {
        log(error, "read()" err);
        return -1;
      }
      if (len == 0) {
        log(info, "connection lost\n");
        return 0;
      }
      if (write_all(STDOUT_FILENO, buf, len) != len) {
        log(error, "write_all()" err);
        return -1;
      }
    }
  }
}
