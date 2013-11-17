#ifndef __UTILITY_H__
#define __UTILITY_H__

#include <sys/types.h>

extern int set_alarm_timer(int milliseconds);
extern int32_t atoi32(const char *value);
extern int add_to_set(int max_fd, int fd, fd_set *set);
extern int set_non_blocking_mode(int fd);
extern int set_blocking_mode(int fd);

#endif /* __UTILITY_H__ */
