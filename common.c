#include <errno.h>
#include <unistd.h>

#include "common.h"


ssize_t read_all (int fd, void *buf, size_t sz)
{
  char *cbuf = (char*)buf;
  ssize_t got;
  size_t pos = 0;

  while (0 < sz) {
    got = read(fd, cbuf, sz);
    if (got < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (got == 0)
      return pos;
    pos  += got;
    cbuf += got;
    sz   -= got;
  }

  return pos;
}


ssize_t write_all (int fd, const void *buf, size_t sz)
{
  const char *cbuf = (const char*)buf;
  ssize_t got;
  size_t pos = 0;

  while (0 < sz) {
    got = write(fd, cbuf, sz);
    if (got < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (got == 0)
      return pos;
    pos  += got;
    cbuf += got;
    sz   -= got;
  }

  return pos;
}
