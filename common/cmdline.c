/*
 * Sailor Utility - Common utilities for Project Sailor
 * Copyright 2013, Baidu Inc.  All rights reserved.
 * Author: yiyuanzhong@gmail.com (Yiyuan Zhong)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Baidu Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * $Author: zhongyiyuan01 $
 * $Date: 2013-05-04 14:07:24 +0800 (Sat, 04 May 2013) $
 * $Revision: 156527 $
 * @brief
 *
 **/


#include "cmdline.h"

#include <assert.h>
#include <errno.h>

#if defined(WIN32)
# include <Windows.h>
#elif __unix__
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_PREFIX_LENGTH 20

extern char **environ;

static size_t g_max_length              = 0;
static size_t g_prefix_length           = 0;
static char   g_prefix[MAX_PREFIX_LENGTH]  ;

static       char *g_arg_name        = NULL;
#endif /* __unix__ */

#define MAX_STATIC_BUFFER 65536

static const char *g_module_arg      = NULL;
static const char *g_module_path     = NULL;
static const char *g_module_dirname  = NULL;
static const char *g_module_basename = NULL;

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

#if defined(WIN32)
char **cmdline_setup(int argc, char *argv[])
{
    char filename[MAX_PATH * 4];
    wchar_t buffer[MAX_PATH];
    char *pos;
    DWORD ret;
    DWORD i;
    int len;

    if (argc <= 0 || !argv || !*argv) {
        return NULL;
    }

    ret = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (ret == 0 || ret == MAX_PATH) {
        return NULL;
    }

    len = WideCharToMultiByte(CP_UTF8, 0, buffer, ret, filename, sizeof(filename), NULL, NULL);
    if (len <= 0 || len >= sizeof(filename)) {
        return NULL;
    }
    filename[len] = '\0';

    for (i = 0; i < ret; ++i) {
        if (filename[i] == '\\') {
            filename[i] = '/';
        }
    }

    g_module_arg = cmdline_strdup(argv[0]);
    g_module_path = cmdline_strdup(filename);

    pos = strrchr(filename, '/');
    if (!pos) {
        g_module_basename = cmdline_strdup(filename);
        g_module_dirname = cmdline_strdup(".");
    } else {
        g_module_basename = cmdline_strdup(pos + 1);
        *pos = '\0';
        g_module_dirname = cmdline_strdup(filename);
    }

    return argv;
}

int cmdline_set_process_name(const char *fmt, ...)
{
    /* NOP */
    return 0;
}
#elif __unix__
/* Both dirname and basename are retrieved. */
static int cmdline_get_module_path_via_exe(void)
{
    char buffer[PATH_MAX];
    char path[128];
    int ret;

    if (g_module_dirname && g_module_basename) {
        return 0;
    }

    snprintf(path, sizeof(path), "/proc/%d/exe", getpid());
    ret = readlink(path, buffer, sizeof(buffer));
    if (ret <= 0) {
        return -1;
    }

    buffer[ret] = '\0';
    g_module_dirname = cmdline_strdup(dirname(cmdline_strdup(buffer)));
    g_module_basename = cmdline_strdup(basename(cmdline_strdup(buffer)));
    return 0;
}

/* Only basename are retrieved. */
static int cmdline_get_module_path_via_stat(const char *argv)
{
    char buffer[NAME_MAX];
    char *argv_name;
    size_t len;
    FILE *file;
    int ret;

    if (g_module_basename) {
        return 0;
    }

    /* In case of ARGV[0] been modified, read module name directly from PROCFS. */
    file = fopen("/proc/self/stat", "r");
    if (!file) {
        return -1;
    }

    ret = fscanf(file, "%*d (%15[^)]", buffer);
    fclose(file);
    if (ret != 1) {
        return -1;
    }

    len = strlen(buffer);
    if (!len) {
        return -1;
    }

    argv_name = basename(cmdline_strdup(argv));

    /* Looks like a truncation, proceed with argv. */
    if (len == 15 && strstr(argv_name, buffer) == argv_name) {
        g_module_basename = cmdline_strdup(argv_name);
    } else {
        g_module_basename = cmdline_strdup(buffer);
    }

    return 0;
}

/* Only dirname is retrieved. */
static int cmdline_get_module_path_via_fd(const char *argv)
{
    char buffer[PATH_MAX];
    char path[128];
    int ret;
    int fd;

    if (g_module_dirname) {
        return 0;
    }

    fd = open(dirname(cmdline_strdup(argv)), O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    snprintf(path, sizeof(path), "/proc/%d/fd/%d", getpid(), fd);
    ret = readlink(path, buffer, sizeof(buffer));

    close(fd);
    if (ret <= 0) {
        return -1;
    }

    buffer[ret] = '\0';
    g_module_dirname = cmdline_strdup(buffer);
    return 0;
}

/* Only dirname is retrieved. */
/* Both dirname and basename is retrieved. */
/* Never fails. */
static int cmdline_get_module_path_via_argv(const char *argv)
{
    if (!g_module_dirname) {
        g_module_dirname = cmdline_strdup(dirname(cmdline_strdup(argv)));
    }

    if (!g_module_basename) {
        g_module_basename = cmdline_strdup(basename(cmdline_strdup(argv)));
    }

    return 0;
}

static int cmdline_get_module_path(const char *argv)
{
    char buffer[PATH_MAX];

    cmdline_get_module_path_via_exe();
    cmdline_get_module_path_via_stat(argv);
    cmdline_get_module_path_via_fd(argv);
    cmdline_get_module_path_via_argv(argv);

    if (!g_module_dirname || !g_module_basename) {
        return -1;
    }

    snprintf(buffer, sizeof(buffer), "%s/%s", g_module_dirname, g_module_basename);
    g_module_path = cmdline_strdup(buffer);
    return 0;
}

static void cmdline_reserve_prefix(void)
{
    size_t len;

    assert(g_module_basename);

    len = strlen(g_module_basename);
    if (len > sizeof(g_prefix) - 2) {
        len = sizeof(g_prefix) - 2;
    }

    g_prefix_length = len + 2;
    memcpy(g_prefix, g_module_basename, len);
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

    if (cmdline_get_module_path(g_arg_name)) {
        return NULL;
    }

    /* Now g_module_basename should be available. */
    cmdline_reserve_prefix();

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

#if !HAVE_SETPROCTITLE
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
#endif /* HAVE_SETPROCTITLE */

#endif /* __unix__ */

const char *cmdline_arg_name()
{
    return g_module_arg;
}

const char *cmdline_module_dirname()
{
    return g_module_dirname;
}

const char *cmdline_module_basename()
{
    return g_module_basename;
}

const char *cmdline_module_path()
{
    return g_module_path;
}
