#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#include "utility.h"

int32_t atoi32(const char *value)
{
    char *end;
    long i;

    if (!value || !*value) {
        return -1;
    }

    i = strtol(value, &end, 10);
    if (end != value + strlen(value)        ||
        (i == LONG_MAX && errno == ERANGE)  ||
        i > INT32_MAX                       ||
        i < 0                               ){

        return -1;
    }

    return (int32_t)i;
}

int add_to_set(int max_fd, int fd, fd_set *set)
{
    assert(set);
    if (fd < 0 || fd >= (int)FD_SETSIZE) {
        return max_fd;
    }

    FD_SET(fd, set);
    if (max_fd < fd) {
        max_fd = fd;
    }

    return max_fd;
}

int set_non_blocking_mode(int fd)
{
    int options = fcntl(fd, F_GETFL);
    if (options < 0) {
        return -1;
    }

    if ((options & O_NONBLOCK) == O_NONBLOCK) {
        return 0;
    }

    options |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, options)) {
        return -1;
    }

    return 0;
}

int set_blocking_mode(int fd)
{
    int options = fcntl(fd, F_GETFL);
    if (options < 0) {
        return -1;
    }

    if ((options & O_NONBLOCK) != O_NONBLOCK) {
        return 0;
    }

    options &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, options)) {
        return -1;
    }

    return 0;
}

int set_socket_address_reuse(int sockfd)
{
    static const int one = 1;
    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, (socklen_t)sizeof(one));
}

int set_alarm_timer(int milliseconds)
{
    struct itimerval timer;
    if (milliseconds < 0) {
        milliseconds = 0;
    }

    timer.it_value.tv_sec = milliseconds / 1000;
    timer.it_value.tv_usec = milliseconds % 1000 * 1000;
    timer.it_interval = timer.it_value;
    if (setitimer(ITIMER_REAL, &timer, NULL)) {
        return -1;
    }

    return 0;
}

int set_maximum_files(int nofile)
{
    struct rlimit rlim;
    struct rlimit old;

    if (nofile < 0) {
        errno = EINVAL;
        return -1;
    }

    if (getrlimit(RLIMIT_NOFILE, &rlim)) {
        return -1;
    }

    if (rlim.rlim_cur == (rlim_t)nofile) {
        return rlim.rlim_cur;
    }

    old = rlim;
    rlim.rlim_cur = nofile;
    if (rlim.rlim_max != RLIM_INFINITY && rlim.rlim_max < (rlim_t)nofile) {
        /* Try to override. */
        rlim.rlim_max = nofile;
        if (setrlimit(RLIMIT_NOFILE, &rlim) == 0) { /* Cool! */
            return rlim.rlim_cur;
        }

        /* No way to get more. */
        if (old.rlim_cur == old.rlim_max) {
            return old.rlim_cur;
        }

        rlim.rlim_cur = rlim.rlim_max = old.rlim_max;
    }

    if (setrlimit(RLIMIT_NOFILE, &rlim)) {
        return old.rlim_cur;
    }

    return rlim.rlim_cur;
}

int set_close_on_exec(int fd)
{
    int ret;
    ret = fcntl(fd, F_GETFD);
    if (ret < 0) {
        return ret;
    }

    ret |= FD_CLOEXEC;
    return fcntl(fd, F_SETFD, ret);
}
