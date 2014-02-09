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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "buffer.h"
#include "config.h"
#include "environ.h"
#include "message.h"

extern char **environ;

static buffer_t g_client_buffer;

static int g_argc;
static char **g_argv;

static int g_client_tty;
static int g_client_pipe;
static int g_client_status;
static struct termios g_client_termp;
static volatile sig_atomic_t g_client_sigterm;
static volatile sig_atomic_t g_client_sigwinch;

static int client_prepare_argv(array_t *argv)
{
    int i;

    for (i = 0; i < g_argc; ++i) {
        if (array_push(argv, g_argv[i])) {
            return -1;
        }
    }

    return 0;
}

static int client_prepare_env(array_t *env)
{
    char **p;

    for (p = environ; *p; ++p) {
        if (!environ_permitted(*p)) {
            continue;
        }

        if (array_push(env, *p)) {
            return -1;
        }
    }

    return 0;
}

static ssize_t client_prepare(void *buffer, size_t length, size_t offset)
{
    array_t argv;
    array_t env;
    ssize_t la;
    ssize_t le;
    char *buf;

    array_initialize(&argv);
    array_initialize(&env);
    buf = (char *)buffer;

    if (client_prepare_argv(&argv) || client_prepare_env(&env)) {
        array_release(&argv);
        array_release(&env);
        return -1;
    }

    la = array_serialize(&argv, buf, length - offset - 1);
    if (la < 0) {
        array_release(&argv);
        array_release(&env);
        return -1;
    }

    le = array_serialize(&env, buf + la, length - offset - la);
    if (le < 0) {
        array_release(&argv);
        array_release(&env);
        return -1;
    }

    array_release(&argv);
    array_release(&env);

    return (ssize_t)(offset + la + le);
 }

static int client_execute_tty(int s)
{
    message_fork_tty_t *msg;
    char buffer[8192];
    ssize_t length;

    msg = (message_fork_tty_t *)buffer;
    msg->type = TypeForkTty;

    length = client_prepare(msg->env, sizeof(buffer), offsetof(message_fork_tty_t, env));
    if (length < 0) {
        return -1;
    }

    msg->length = (size_t)length;
    msg->termp = g_client_termp;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &msg->winp)) {
        return -1;
    }

    if (write(s, buffer, msg->length) != msg->length) {
        return -1;
    }

    return 0;
}

static int client_execute_pipe(int s)
{
    message_fork_pipe_t *msg;
    char buffer[8192];
    ssize_t length;

    msg = (message_fork_pipe_t *)buffer;
    msg->type = TypeForkPipe;

    length = client_prepare(msg->env, sizeof(buffer), offsetof(message_fork_pipe_t, env));
    if (length < 0) {
        return -1;
    }

    msg->length = (size_t)length;

    if (write(s, buffer, msg->length) != msg->length) {
        return -1;
    }

    return 0;
}

static int client_execute_socket(int s)
{
    message_fork_socket_t *msg;
    char buffer[8192];
    ssize_t length;

    msg = (message_fork_socket_t *)buffer;
    msg->type = TypeForkSocket;

    length = client_prepare(msg->env, sizeof(buffer), offsetof(message_fork_socket_t, env));
    if (length < 0) {
        return -1;
    }

    msg->length = (size_t)length;

    if (write(s, buffer, msg->length) != msg->length) {
        return -1;
    }

    return 0;
}

static int client_execute(int s)
{
    if (g_client_tty) {
        return client_execute_tty(s);
    } else if (g_client_pipe) {
        return client_execute_pipe(s);
    } else {
        return client_execute_socket(s);
    }
}

static int client_start(const char *path)
{
    struct sockaddr_un addr;
    int len;
    int ret;
    int s;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    len = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (len < 0) {
        return -1;
    } else if ((size_t)len >= sizeof(addr.sun_path)) {
        len = sizeof(struct sockaddr_un) - 1;
    } else {
        len += offsetof(struct sockaddr_un, sun_path);
    }

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }

    ret = connect(s, (struct sockaddr *)&addr, (socklen_t)len);
    if (ret < 0) {
        close(s);
        return -1;
    }

    if (client_execute(s)) {
        close(s);
        return -1;
    }

    return s;
}

static int client_process_remote_stdio(int fd, const message_t *msg)
{
    size_t length;
    ssize_t ret;

    if (msg->length < sizeof(*msg)) {
        return -1;
    }

    length = msg->length - sizeof(*msg) + 1;

    ret = write(fd, msg->body, length);
    if (ret < 0 || (size_t)ret != length) {
        return -1;
    }

    return 0;
}

static int client_process_remote_status(const message_t *msg)
{
    message_status_t *m;

    if (msg->length < sizeof(*msg)) {
        return -1;
    }

    m = (message_status_t *)msg;
    g_client_status = m->status;
    g_client_sigterm = 1;
    return 0;
}

static int client_dispatch_remote(const message_t *msg)
{
    switch (msg->type) {
    case TypeStdOut:
        return client_process_remote_stdio(STDOUT_FILENO, msg);

    case TypeStdErr:
        return client_process_remote_stdio(STDERR_FILENO, msg);

    case TypeStatus:
        return client_process_remote_status(msg);

    default:
        return -1;
    };
}

static int client_process_remote(int s)
{
    message_t *msg;
    void *buffer;
    size_t space;
    ssize_t size;
    int ret;

    buffer = buffer_get_buffer(&g_client_buffer);
    space = buffer_get_buffer_space(&g_client_buffer);
    size = read(s, buffer, space);
    if (size <= 0) {
        return -1;
    }

    buffer_push_buffer(&g_client_buffer, size);

    for (;;) {
        ret = buffer_get_message(&g_client_buffer, &msg);
        if (ret < 0) {
            return -1;
        } else if (ret > 0) {
            break;
        }

        ret = client_dispatch_remote(msg);
        if (ret) {
            return -1;
        }

        buffer_consume_message(&g_client_buffer, msg);
    }

    return 0;
}

static int client_process_input(int s)
{
    message_stdin_t *msg;
    char buffer[8192];
    ssize_t size;

    size = read(STDIN_FILENO,
                buffer + sizeof(*msg) - 1,
                sizeof(buffer) - sizeof(msg) + 1);

    if (size <= 0) {
        return -1;
    }

    msg = (message_stdin_t *)buffer;
    msg->type = TypeStdIn;
    msg->length = sizeof(*msg) + size - 1;

    size = write(s, msg, msg->length);
    if (size != msg->length) {
        return -1;
    }

    return 0;
}

static int client_sigwinch(int s)
{
    message_winsize_t msg;

    if (!g_client_sigwinch) {
        return 0;
    }

    msg.type = TypeWinsize;
    msg.length = sizeof(msg);
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &msg.winp)) {
        return -1;
    }

    if (write(s, &msg, sizeof(msg)) != sizeof(msg)) {
        return -1;
    }

    g_client_sigwinch = 0;
    return 0;
}

static int client_loop(int s)
{
    struct timespec tv;
    sigset_t set;
    fd_set fds;
    int ret;

    FD_ZERO(&fds);
    sigemptyset(&set);
    while (!g_client_sigterm) {
        if (client_sigwinch(s)) {
            return -1;
        }

        /* In case that pselect(2) is emulated by glibc. */
        tv.tv_sec = 5;
        tv.tv_nsec = 0;
        FD_SET(s,            &fds);
        FD_SET(STDIN_FILENO, &fds);
        ret = pselect(s + 1, &fds, NULL, NULL, &tv, &set);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        } else if (ret == 0) {
            continue;
        }

        if (FD_ISSET(s, &fds)) {
            if (client_process_remote(s)) {
                return -1;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (client_process_input(s)) {
                return -1;
            }
        }
    }

    return 0;
}

static void client_atexit(void)
{
    if (g_client_tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_client_termp);
    }
}

static void sigterm_handler(int signum)
{
    (void)signum;
    g_client_sigterm = 1;
}

static void sigwinch_handler(int signum)
{
    (void)signum;
    g_client_sigwinch = 1;
}

static int client_initialize_signals(void)
{
    sigset_t set;
    struct sigaction act;

    if (sigemptyset(&set)                   ||
        sigaddset(&set, SIGHUP)             ||
        sigaddset(&set, SIGINT)             ||
        sigaddset(&set, SIGQUIT)            ||
        sigaddset(&set, SIGTERM)            ||
        sigaddset(&set, SIGWINCH)           ||
        sigprocmask(SIG_BLOCK, &set, NULL)  ){

        return -1;
    }

    memset(&act, 0, sizeof(act));
    if (sigemptyset(&act.sa_mask)) {
        return -1;
    }

    act.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &act, NULL)) {
        return -1;
    }

    act.sa_handler = sigwinch_handler;
    if (sigaction(SIGWINCH, &act, NULL)) {
        return -1;
    }

    act.sa_handler = sigterm_handler;
    if (sigaction(SIGHUP,  &act, NULL) ||
        sigaction(SIGINT,  &act, NULL) ||
        sigaction(SIGQUIT, &act, NULL) ||
        sigaction(SIGTERM, &act, NULL) ){

        return -1;
    }

    return 0;
}

static int client_initialize(void)
{
    struct termios termp;
    struct stat st;

    g_client_tty = 0;
    g_client_pipe = 0;
    g_client_status = -1;
    g_client_sigterm = 0;
    g_client_sigwinch = 0;

    buffer_initialize(&g_client_buffer);

    if (client_initialize_signals()) {
        return -1;
    }

    if (isatty(STDIN_FILENO)) {
        g_client_tty = 1;

    } else {
        if (fstat(STDIN_FILENO, &st)) {
            return -1;
        }

        if (S_ISSOCK(st.st_mode)) {
            g_client_pipe = 0;
        } else if (S_ISFIFO(st.st_mode)) {
            g_client_pipe = 1;
        } else { /* TODO: /dev/null? */
            return -1;
        }

        return 0;
    }

    if (tcgetattr(STDIN_FILENO, &g_client_termp)) {
        return -1;
    }

    termp = g_client_termp;
    cfmakeraw(&termp);

    if (atexit(client_atexit)) {
        return -1;
    }

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termp)) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    char buffer[PATH_MAX];
    const char *socket;
    int ret;
    int s;

    g_argc = argc;
    g_argv = argv;

    if (client_initialize()) {
        return 255;
    }

    socket = getenv("SOCKET");
    if (!socket) {
        socket = kSocketPrefix;
    }

    ret = snprintf(buffer, sizeof(buffer), "%s-%d-%d.sock", socket, getuid(), getgid());
    if (ret < 0 || (size_t)ret >= sizeof(buffer)) {
        return 255;
    }

    s = client_start(buffer);
    if (s < 0) {
        return 255;
    }

    if (client_loop(s)) {
        return 255;
    }

    if (!WIFEXITED(g_client_status)) {
        return 255;
    }

    return WEXITSTATUS(g_client_status);
}
