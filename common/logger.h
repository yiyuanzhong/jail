#ifndef __LOGGER_H__
#define __LOGGER_H__

typedef struct {
    ssize_t (*emerg)(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
    ssize_t (*error)(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
    ssize_t (*warn )(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
    ssize_t (*info )(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
    ssize_t (*trace)(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
    ssize_t (*debug)(const char *args, ...) __attribute__ ((format (printf, 1, 2)));
} __logger_t;

extern __logger_t g_logger;
extern const char *__file;
extern int __line;

#define LOG __file = __FILE__; __line = __LINE__; g_logger

#endif /* __LOGGER_H__ */
