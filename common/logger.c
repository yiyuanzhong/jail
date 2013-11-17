#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

const char *__file;
int __line;

static ssize_t logger_print(const char *level, const char *file, int line,
                            const char *format, va_list va,
                            void *buffer, size_t size)
{
    struct timeval tv;
    struct tm *tm;
    size_t total;
    ssize_t ret;
    char *p;

    p = (char *)buffer;
    total = 0;

    if (gettimeofday(&tv, NULL)) {
        return -1;
    }

    tm = localtime(&tv.tv_sec);
    if (!tm) {
        return -1;
    }

    ret = snprintf(p, size, "%d\t%s\t%04d-%02d-%02d %02d:%02d:%02d.%03ld ", getpid(), level,
                   tm->tm_year + 1970, tm->tm_mon + 1, tm->tm_mday,
                   tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec / 1000);

    if (ret < 0) {
        return -1;
    } else if ((size_t)ret >= size) {
        total += size;
        return total;
    }

    total += ret;
    size -= ret;
    p += ret;

    ret = vsnprintf(p, size, format, va);
    if (ret < 0) {
        return -1;
    } else if ((size_t)ret >= size) {
        total += size;
        return total;
    }

    total += ret;
    size -= ret;
    p += ret;

    ret = snprintf(p, size, " [%s:%d]\n", file, line);
    if (ret < 0) {
        return -1;
    } else if ((size_t)ret >= size) {
        total += size;
        return total;
    }

    total += ret;
    size -= ret;
    p += ret;

    return total;
}

static ssize_t logger_log(const char *level, const char *format, va_list va)
{
    char buffer[2048];
    ssize_t ret;
    size_t size;
    int fd;

    ret = logger_print(level, __file, __line, format, va, buffer, sizeof(buffer));
    if (ret < 0) {
        return -1;
    } else if (ret == 0) {
        return 0;
    }

    fd = open("/tmp/log", O_CREAT | O_APPEND | O_WRONLY, DEFFILEMODE);
    if (fd < 0) {
        return -1;
    }

    size = (size_t)ret;
    ret = write(fd, buffer, size);
    close(fd);
    return ret;
}

#define LOGGER(name,level) \
static ssize_t logger_##name(const char *format, ...) \
{ \
    ssize_t ret; \
    va_list va; \
    va_start(va, format); \
    ret = logger_log(level, format, va); \
    va_end(va); \
    return ret; \
}

LOGGER(emerg, "FATAL"  );
LOGGER(error, "ERROR"  );
LOGGER(warn,  "WARNING");
LOGGER(info,  "NOTICE" );
LOGGER(trace, "TRACE"  );
LOGGER(debug, "DEBUG"  );

__logger_t g_logger = {
    logger_emerg,
    logger_error,
    logger_warn,
    logger_info,
    logger_trace,
    logger_debug,
};
