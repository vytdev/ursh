#ifndef URSH_COMMON_H_
#define URSH_COMMON_H_   1

#include <sys/types.h>  /* ssize_t */

/* read all. handle partial reads */
ssize_t read_all (int fd, void *buf, size_t sz);

/* write all. deal with partial writes */
ssize_t write_all (int fd, const void *buf, size_t sz);

#endif /* URSH_COMMON_H_ */
