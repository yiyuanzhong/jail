/* Copyright 2014 yiyuanzhong@gmail.com (Yiyuan Zhong)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cmdline.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PREFIX_LENGTH 20
#define MAX_STATIC_BUFFER 65536

extern char **environ;

static size_t g_max_length              = 0;
static size_t g_prefix_length           = 0;
static char   g_prefix[MAX_PREFIX_LENGTH]  ;

static       char *g_arg_name        = NULL;

static const char *g_module_arg      = NULL;

static void *cmdline_malloc(size_t size)
{
    static char s_buffer[MAX_STATIC_BUFFER];
    static size_t s_size = sizeof(s_buffer);
    static char *s_ptr = s_buffer;

    char *ret;
    if (size > s_size) {
        return malloc(size);
    }

    ret = s_ptr;
    s_ptr += size;
    s_size -= size;
    return ret;
}

static char *cmdline_strdup(const char *str)
{
    char *ret;
    size_t len;
    if (!str) {
        return NULL;
    }

    len = strlen(str);
    ret = cmdline_malloc(len + 1);
    if (!ret) {
        return NULL;
    }

    memcpy(ret, str, len + 1);
    return ret;
}

static void cmdline_reserve_prefix(char *argv0)
{
    size_t len;
    char *base;

    base = basename(cmdline_strdup(argv0));
    len = strlen(base);
    if (len > sizeof(g_prefix) - 2) {
        len = sizeof(g_prefix) - 2;
    }

    g_prefix_length = len + 2;
    memcpy(g_prefix, base, len);
    g_prefix[len++] = ':';
    g_prefix[len++] = ' ';
}

char **cmdline_setup(int argc, char *argv[])
{
    char **environ_duplication;
    char **argv_duplication;
    size_t environ_size;
    char *pointer;
    int argv_size;
    size_t i;
    int j;

    if (argc <= 0 || !argv || !*argv || g_arg_name) {
        errno = EINVAL;
        return NULL;
    }

    g_arg_name = argv[0];
    g_max_length = strlen(g_arg_name);
    g_module_arg = cmdline_strdup(argv[0]);

    cmdline_reserve_prefix(argv[0]);

    /* Try to count real ARGV length. */
    argv_size = 1;
    while (argv_size < argc && argv[argv_size]) {
        if (argv[argv_size] != argv[argv_size - 1] + strlen(argv[argv_size - 1]) + 1) {
            break;
        }
        ++argv_size;
    }

    /* Try to count available environment variables. */
    environ_size = 0;
    if (argv_size == argc) {
        if (environ && *environ) {
            pointer = argv[argc - 1];
            while (environ[environ_size]) {
                if (environ[environ_size] != pointer + strlen(pointer) + 1) {
                    break;
                }
                pointer = environ[environ_size];
                ++environ_size;
            }
        }
    }

    argv_duplication = cmdline_malloc(sizeof(char *) * (argv_size + 1));
    if (!argv_duplication) {
        errno = ENOMEM;
        return NULL;
    }

    for (j = 0; j < argv_size; ++j) {
        argv_duplication[j] = cmdline_strdup(argv[j]);
    }
    argv_duplication[argc] = NULL;

    /* Not available, just occupy ARGV. */
    if (!environ_size) {
        g_max_length = argv[argv_size - 1] - argv[0] + strlen(argv[argv_size - 1]);
        return argv_duplication;
    }

    environ_duplication = cmdline_malloc(sizeof(char *) * (environ_size + 1));
    if (!environ_duplication) {
        return argv_duplication;
    }

    g_max_length = environ[environ_size - 1] - argv[0] + strlen(environ[environ_size - 1]);
    for (i = 0; i < environ_size; ++i) {
        environ_duplication[i] = cmdline_strdup(environ[i]);
    }
    environ_duplication[environ_size] = NULL;

    environ = environ_duplication;
    return argv_duplication;
}

int cmdline_set_process_name(const char *fmt, ...)
{
    int with_prefix;
    int eraser;
    va_list ap;
    size_t max;
    size_t off;

    if (!g_max_length || !g_arg_name) {
        errno = EINVAL;
        return -1;
    }

    if (!fmt) {
        fmt = g_module_arg;
        with_prefix = 0;

    } else if (*fmt == '-') {
        with_prefix = 0;
        ++fmt;

    } else {
        with_prefix = g_prefix_length ? 1 : 0;
    }

    if (with_prefix) {
        if (g_prefix_length <= g_max_length) {
            off = g_prefix_length;
        } else {
            off = g_max_length;
        }

        memcpy(g_arg_name, g_prefix, off);
        max = g_max_length - off;

    } else {
        max = g_max_length;
        off = 0;
    }

    /* g_max_length doesn't include the trailing zero, so add 1 to it. */
    va_start(ap, fmt);
    eraser = vsnprintf(g_arg_name + off, max + 1, fmt, ap);
    va_end(ap);
    if (eraser < 0) {
        return -1;
    }

    /* And we erase all remaining buffer to 0, since some OS might remember the old length. */
    if (max > (size_t)eraser) {
        off += eraser;
    } else {
        off += max;
    }

    memset(g_arg_name + off, 0, g_max_length - off);
    return 0;
}
