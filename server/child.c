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

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>

#include "array.h"
#include "buffer.h"
#include "cmdline.h"
#include "config.h"
#include "environ.h"
#include "message.h"
#include "utility.h"

#include "jaild.h"

extern char **environ;

static int          g_child_remote;     /* Remote socket fd. */
static pid_t        g_child_pid;        /* -1 if not started, 0 if exited. */
static int          g_child_pty;        /* Whether it's pty. */
static int          g_child_stdin;
static int          g_child_stdout;
static int          g_child_stderr;
static buffer_t     g_child_buffer;
static int          g_child_handshaking;

static volatile sig_atomic_t g_child_sigalrm;
static volatile sig_atomic_t g_child_sigchld;

static int child_drop_privileges(void)
{
    if (geteuid()) {
        return 0;
    }

    if (setgroups(1, &g_args_gid)   ||
        setgid(g_args_gid)          ||
        setuid(g_args_uid)          ){

        return -1;
    }

    return 0;
}

static void child_sigalrm_handler(int signum)
{
    (void)signum;
    g_child_sigalrm = 1;
}

static void child_sigchld_handler(int signum)
{
    (void)signum;
    g_child_sigchld = 1;
}

static int child_initialize_signals(void)
{
    struct sigaction act;
    sigset_t set;
    int i;

    /* Restore every signal. */
    memset(&act, 0, sizeof(act));
    if (sigemptyset(&act.sa_mask)) {
        return -1;
    }

    act.sa_handler = SIG_DFL;
    for (i = 1; i < NSIG; ++i) {
        if (sigaction(i, &act, NULL)) {
            if (errno != EINVAL) {
                return -1;
            }
        }
    }

    /* Blocks useful signals. */
    if (sigemptyset(&set)                       ||
        sigaddset(&set, SIGALRM)                ||
        sigaddset(&set, SIGCHLD)                ||
        sigprocmask(SIG_SETMASK, &set, NULL)    ){

        return -1;
    }

    /* And deal with some. */
    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &act, NULL)) {
        return -1;
    }

    act.sa_handler = child_sigalrm_handler;
    if (sigaction(SIGALRM, &act, NULL)) {
        return -1;
    }

    act.sa_handler = child_sigchld_handler;
    if (sigaction(SIGCHLD, &act, NULL)) {
        return -1;
    }

    return 0;
}

static const char *child_get_name(struct passwd *pwd)
{
    const char *name;

    name = g_args_name;
    if (!name) {
        name = pwd->pw_name;
    }

    return name;
}

static const char *child_get_shell(struct passwd *pwd)
{
    const char *shell;

    shell = g_args_shell;
    if (!shell) {
        shell = kDefaultShell;
        if (pwd->pw_shell && pwd->pw_shell[0] == '/') {
            shell = pwd->pw_shell;
        }
    }

    return shell;
}

static int child_setup_envvars(struct passwd *pwd, array_t *env)
{
    const char *shell;
    const char *name;
    size_t count;
    size_t i;
    char *e;

    count = array_get_count(env);
    for (i = 0; i < count; ++i) {
        e = array_get_element(env, i);
        assert(e);

        if (!environ_permitted(e)) {
            continue;
        }

        if (putenv(e)) {
            return -1;
        }
    }

    /* Override some of the environment variables. */
    shell = child_get_shell(pwd);
    if (setenv("SHELL", shell, 1)) {
        return -1;
    }

    name = child_get_name(pwd);
    if (setenv("LOGNAME", name, 1) ||
#ifdef _AIX
        setenv("LOGIN",   name, 1) ||
#endif
        setenv("USER",    name, 1) ){

        return -1;
    }

    /* Modify SSH_TTY. */
    if (getenv("SSH_TTY")) {
        if (g_child_pty) {
            if (setenv("SSH_TTY", ttyname(STDIN_FILENO), 1)) {
                return -1;
            }
        } else {
            if (unsetenv("SSH_TTY")) {
                return -1;
            }
        }
    }

    return 0;
}

static char **child_construct_args(struct passwd *pwd, array_t *argv)
{
    char shell[NAME_MAX];
    const char *sh;
    size_t count;
    char **args;
    size_t size;
    size_t i;
    char *s;
    int ret;

    sh = array_get_element(argv, 0);
    if (!sh || *sh == '-') {
        size = sizeof(shell) - 1;
        shell[0] = '-';
        s = shell + 1;
    } else {
        size = sizeof(shell);
        s = shell;
    }

    sh = child_get_shell(pwd);
    sh = strrchr(sh, '/');
    assert(sh);
    ++sh;

    ret = snprintf(s, size, "%s", sh);
    if (ret < 0 || (size_t)ret >= size) {
        return NULL;
    }

    count = array_get_count(argv);
    if (count == 0) { /* WTF? */
        count = 1;
    }

    args = (char **)malloc(sizeof(char *) * (count + 1));
    if (!args) {
        return NULL;
    }

    args[0] = shell;
    for (i = 1; i < count; ++i) {
        args[i] = array_get_element(argv, i);
    }

    args[i] = NULL;
    return args;
}

static int child_restore_signals(void)
{
    struct sigaction act;
    sigset_t set;
    int i;

    memset(&act, 0 ,sizeof(act));
    if (sigemptyset(&act.sa_mask) || sigemptyset(&set)) {
        return -1;
    }

    if (sigprocmask(SIG_SETMASK, &set, NULL)) {
        return -1;
    }

    act.sa_handler = SIG_DFL;
    for (i = 1; i < NSIG; ++i) {
        if (sigaction(i, &act, NULL)) {
            if (errno != EINVAL) {
                return -1;
            }
        }
    }

    return 0;
}

static int child_chdir_to_home(struct passwd *pwd)
{
    if (setenv("HOME", pwd->pw_dir, 1)) {
        return -1;
    }

    if (chdir(pwd->pw_dir) == 0) {
        if (setenv("PWD", pwd->pw_dir, 1) == 0) {
            return 0;
        }
    }

    if (chdir("/") == 0) {
        if (setenv("PWD", "/", 1) == 0) {
            return 0;
        }
    }

    return -1;
}

static void child_execve(struct passwd *pwd, char *args[])
{
    const char *shell;

    shell = child_get_shell(pwd);
    execve(shell, args, environ);
}

static void child_exec_child(array_t *argv, array_t *env)
{
    struct passwd *pwd;
    char **args;

    /* Reset all envvars. */
    environ[0] = NULL;

    if (!g_child_pty) {
        if (setsid() < 0) {
            _exit(EXIT_FAILURE);
        }
    }

    if (child_restore_signals()) {
        _exit(EXIT_FAILURE);
    }

    pwd = getpwuid(g_args_uid);
    if (!pwd) {
        _exit(EXIT_FAILURE);
    }

    if (child_chdir_to_home(pwd)) {
        _exit(EXIT_FAILURE);
    }

    if (child_setup_envvars(pwd, env)) {
        _exit(EXIT_FAILURE);
    }

    args = child_construct_args(pwd, argv);
    if (!args) {
        _exit(EXIT_FAILURE);
    }

    child_execve(pwd, args);

    free(args);
    _exit(EXIT_FAILURE);
}

/* POSIX way to implement openpty(3). */
static int child_openpty(int *amaster, int *aslave,
                         const struct termios *termp,
                         const struct winsize *winp)
{
    int master;
    int slave;
    char *pts;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        return -1;
    }

    pts = ptsname(master);
    if (!pts || unlockpt(master)) {
        close(master);
        return -1;
    }

    slave = open(pts, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        close(master);
        return -1;
    }

    if (tcsetattr(master, TCSANOW, termp)   ||
        ioctl(slave, TIOCSWINSZ, winp)      ){

        close(slave);
        close(master);
        return -1;
    }

    *amaster = master;
    *aslave = slave;
    return 0;
}

/* POSIX way to implement login_tty(3). */
static int child_login_tty(int aslave)
{
    if (setsid() < 0                        ||
        ioctl(aslave, TIOCSCTTY, aslave)    ||
        dup2(aslave, STDIN_FILENO) < 0      ||
        dup2(aslave, STDOUT_FILENO) < 0     ||
        dup2(aslave, STDERR_FILENO) < 0     ){

        return -1;
    }

    if (aslave != STDIN_FILENO  &&
        aslave != STDOUT_FILENO &&
        aslave != STDERR_FILENO &&
        close(aslave)           ){

        return -1;
    }

    return 0;
}

static pid_t child_fork_pty(const struct termios *termp,
                            const struct winsize *winp,
                            array_t *argv,
                            array_t *env)
{
    int amaster;
    int aslave;
    pid_t pid;

    g_child_pty = 1;

    if (child_openpty(&amaster, &aslave, termp, winp)) {
        return -1;
    }

    /*
     * grantpt(3) should be called as an unprivileged process, but it turns
     * out to do some privileged operations like chown(2) the slave side of
     * the pesudoterminal. In order to do that it executes a SUID helper
     * binary `pt_chown`, however the exact location of the binary varies
     * accross different distros. Once chroot(2)ed, the libc might not be
     * able to find the binary.
     *
     * grantpt(3) changes group of the pesudoterminal to a unspecified value,
     * typically `tty` with GID=5. Since group name and/or GID might vary
     * between inside and outside, it's inproper to read the group database.
     *
     * Here I hardcode the GID as 5.
     */
    if (fchown(aslave, g_args_uid, 5)               ||
        fchmod(aslave, S_IRUSR | S_IWUSR | S_IWGRP) ){

        close(amaster);
        close(aslave);
        return -1;
    }

    if (child_drop_privileges()) {
        _exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        close(amaster);
        close(aslave);
        return -1;

    } else if (pid == 0) {
        close(amaster);
        close(g_child_remote);
        if (child_login_tty(aslave)) {
            _exit(EXIT_FAILURE);
        }

        child_exec_child(argv, env);

    } else {
        cmdline_set_process_name("[%d] %s", pid, ttyname(aslave));
        g_child_stdin  = amaster;
        g_child_stdout = dup(amaster);
        g_child_stderr = -1;
        close(aslave);
    }

    return pid;
}

static int child_fork_socket(array_t *argv, array_t *env)
{
    int sio[2];
    int eio[2];
    pid_t pid;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sio)) {
        return -1;
    }

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, eio)) {
        close(sio[0]);
        close(sio[1]);
        return -1;
    }

    if (child_drop_privileges()) {
        _exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        close(sio[0]);
        close(sio[1]);
        close(eio[0]);
        close(eio[1]);
        return -1;

    } else if (pid == 0) {
        dup2(sio[0], STDIN_FILENO);
        dup2(sio[0], STDOUT_FILENO);
        dup2(eio[0], STDERR_FILENO);
        close(g_child_remote);
        close(sio[0]);
        close(sio[1]);
        close(eio[0]);
        close(eio[1]);
        child_exec_child(argv, env);

    } else {
        g_child_stdin   = sio[1];
        g_child_stdout  = dup(sio[1]);
        g_child_stderr  = eio[1];
        close(sio[0]);
        close(eio[0]);

        cmdline_set_process_name("[%d] socket", pid);
    }

    return pid;
}

static int child_fork_pipe(array_t *argv, array_t *env)
{
    int perr[2];
    int pout[2];
    int pin[2];
    pid_t pid;

    if (pipe(pin)) {
        return -1;
    }

    if (pipe(pout)) {
        close(pin[0]);
        close(pin[1]);
        return -1;
    }

    if (pipe(perr)) {
        close(pout[0]);
        close(pout[1]);
        close(pin[0]);
        close(pin[1]);
        return -1;
    }

    if (child_drop_privileges()) {
        _exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        close(perr[0]);
        close(perr[1]);
        close(pout[0]);
        close(pout[1]);
        close(pin[0]);
        close(pin[1]);
        return -1;

    } else if (pid == 0) {
        dup2(pin[0], STDIN_FILENO);
        dup2(pout[1], STDOUT_FILENO);
        dup2(perr[1], STDERR_FILENO);
        close(g_child_remote);
        close(perr[0]);
        close(perr[1]);
        close(pout[0]);
        close(pout[1]);
        close(pin[0]);
        close(pin[1]);
        child_exec_child(argv, env);

    } else {
        g_child_stdin   = pin[1];
        g_child_stdout  = pout[0];
        g_child_stderr  = perr[0];
        close(perr[1]);
        close(pout[1]);
        close(pin[0]);

        cmdline_set_process_name("[%d] pipe", pid);
    }

    return pid;
}

static int child_fork(int socket_or_pipe,
                      const struct termios *termp,
                      const struct winsize *winp,
                      array_t *argv,
                      array_t *env)
{
    pid_t pid;

    if (g_child_pid >= 0) { /* Already started. */
        return -1;
    }

    /* child_exec_child() needs this. */
    g_child_pty = termp ? 1 : 0;

    if (termp) {
        pid = child_fork_pty(termp, winp, argv, env);
    } else if (socket_or_pipe) {
        pid = child_fork_socket(argv, env);
    } else {
        pid = child_fork_pipe(argv, env);
    }

    if (pid < 0) {
        return -1;
    }

    g_child_pid = pid;
    return 0;
}

static int child_parse(void *buffer,
                       size_t length,
                       size_t offset,
                       array_t *argv,
                       array_t *env)
{
    ssize_t ret;
    char *buf;

    buf = (char *)buffer;

    ret = array_parse(argv, buf, length - offset - 1);
    if (ret < 0) {
        return -1;
    }

    ret = array_parse(env, buf + ret, length - offset - ret);
    if (ret < 0) {
        array_cleanup(argv);
        return -1;
    }

    return 0;
}

static int child_execute_fork_socket(message_fork_socket_t *msg)
{
    array_t argv;
    array_t env;
    int ret;

    if (child_parse(msg->env, msg->length,
                    offsetof(message_fork_socket_t, env),
                    &argv, &env)) {

        return -1;
    }

    ret = child_fork(1, NULL, NULL, &argv, &env);

    array_cleanup(&argv);
    array_cleanup(&env);
    return ret;
}

static int child_execute_fork_pipe(message_fork_socket_t *msg)
{
    array_t argv;
    array_t env;
    int ret;

    if (child_parse(msg->env, msg->length,
                    offsetof(message_fork_pipe_t, env),
                    &argv, &env)) {

        return -1;
    }

    ret = child_fork(0, NULL, NULL, &argv, &env);

    array_cleanup(&argv);
    array_cleanup(&env);
    return ret;
}

static int child_execute_fork_tty(message_fork_tty_t *msg)
{
    array_t argv;
    array_t env;
    int ret;

    if (child_parse(msg->env, msg->length,
                    offsetof(message_fork_tty_t, env),
                    &argv, &env)) {

        return -1;
    }

    ret = child_fork(0, &msg->termp, &msg->winp, &argv, &env);

    array_cleanup(&argv);
    array_cleanup(&env);
    return ret;
}

static int child_execute_winsize(message_winsize_t *msg)
{
    if (!g_child_pty) { /* Just ignore. */
        return 0;
    }

    if (ioctl(g_child_stdin, TIOCSWINSZ, &msg->winp)) {
        return -1;
    }

    return 0;
}

static int child_execute_stdin(message_stdin_t *msg)
{
    size_t length;
    ssize_t size;

    length = msg->length - sizeof(*msg) + 1;
    size = write(g_child_stdin, msg->body, length);
    if (size < 0 || (size_t)size != length) {
        return -1;
    }

    return 0;
}

static int child_execute(message_t *msg)
{
    switch (msg->type) {
    case TypeForkSocket:
        if (msg->length < sizeof(message_fork_socket_t)) {
            return -1;
        }
        return child_execute_fork_socket((message_fork_socket_t *)msg);

    case TypeForkPipe:
        if (msg->length < sizeof(message_fork_pipe_t)) {
            return -1;
        }
        return child_execute_fork_pipe((message_fork_pipe_t *)msg);

    case TypeForkTty:
        if (msg->length < sizeof(message_fork_tty_t)) {
            return -1;
        }
        return child_execute_fork_tty((message_fork_tty_t *)msg);

    case TypeWinsize:
        if (msg->length != sizeof(message_winsize_t)) {
            return -1;
        }
        return child_execute_winsize((message_winsize_t *)msg);

    case TypeStdIn:
        if (msg->length < sizeof(message_stdin_t)) {
            return -1;
        }
        return child_execute_stdin((message_stdin_t *)msg);

    default:
        return -1;
    };
}

static int child_process_remote(int c)
{
    message_t *msg;
    size_t space;
    void *buffer;
    ssize_t size;
    int ret;

    buffer = buffer_get_buffer(&g_child_buffer);
    space = buffer_get_buffer_space(&g_child_buffer);

    size = read(c, buffer, space);
    if (size <= 0) {
        return -1;
    }

    buffer_push_buffer(&g_child_buffer, size);

    for (;;) {
        ret = buffer_get_message(&g_child_buffer, &msg);
        if (ret < 0) {
            return -1;
        } else if (ret > 0) {
            break;
        }

        ret = child_execute(msg);
        if (ret) {
            return -1;
        }

        buffer_consume_message(&g_child_buffer, msg);
    }

    return 0;
}

static int child_run()
{
    if (g_child_sigalrm) {
        if (g_child_handshaking) {
            g_child_handshaking = 0;
            if (set_alarm_timer(0)) {
                return 0;
            }
        }

        if (g_child_pid < 0) {
            return 0;
        }
    }

    if (g_child_stdout >= 0 || g_child_stderr >= 0) {
        return 1;
    }

    if (g_child_pid != 0) {
        return 1;
    }

    return 0;
}

static int child_process_output(int c, int *fd, int type)
{
    char buffer[8192];
    message_t *msg;
    ssize_t size;

    size = read(*fd,
                buffer + sizeof(*msg) - 1,
                sizeof(buffer) - sizeof(msg) + 1);

    if (size <= 0) {
        close(*fd);
        *fd = -1;
        return 0;
    }

    msg = (message_t *)buffer;
    msg->type = type;
    msg->length = sizeof(*msg) + size - 1;

    size = write(c, msg, msg->length);
    if (size != msg->length) {
        return -1;
    }

    return 0;
}

static int child_sigchld(void)
{
    message_status_t msg;
    int status;
    pid_t pid;
    int ret;

    if (!g_child_sigchld) {
        return 0;
    }

    ret = 0;
    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            if (pid == g_child_pid) {
                g_child_pid = 0;
                ret = 1;

                msg.type = TypeStatus;
                msg.length = sizeof(msg);
                msg.status = status;
                if (write(g_child_remote, &msg, sizeof(msg)) != sizeof(msg)) {
                    return -1;
                }
            }

            continue;
        }

        if (pid < 0) {
            if (errno != ECHILD) {
                return -1;
            }
        }

        break;
    }

    g_child_sigchld = 0;
    return ret;
}

static int child_loop(int c)
{
    struct timespec tv;
    sigset_t set;
    fd_set fds;
    int max;
    int ret;

    FD_ZERO(&fds);
    sigemptyset(&set);
    while (child_run()) {
        if (child_sigchld()) {
            return -1;
        }

        max = -1;
        max = add_to_set(max, c,                &fds);
        max = add_to_set(max, g_child_stdout,   &fds);
        max = add_to_set(max, g_child_stderr,   &fds);

        /* In case that pselect(2) is emulated by glibc. */
        tv.tv_sec = 5;
        tv.tv_nsec = 0;
        ret = pselect(max + 1, &fds, NULL, NULL, &tv, &set);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        } else if (ret == 0) {
            continue;
        }

        if (FD_ISSET(c, &fds)) {
            if (child_process_remote(c)) {
                return -1;
            }
        }

        if (g_child_stdout >= 0 && FD_ISSET(g_child_stdout, &fds)) {
            if (child_process_output(c, &g_child_stdout, TypeStdOut)) {
                return -1;
            }
        }

        if (g_child_stderr >= 0 && FD_ISSET(g_child_stderr, &fds)) {
            if (child_process_output(c, &g_child_stderr, TypeStdErr)) {
                return -1;
            }
        }
    }

    return 0;
}

int child_main(int c)
{
    int ret;

    g_child_remote      =  c;
    g_child_pid         = -1;
    g_child_pty         =  0;
    g_child_stdin       = -1;
    g_child_stdout      = -1;
    g_child_stderr      = -1;
    g_child_sigalrm     =  0;
    g_child_sigchld     =  0;
    g_child_handshaking =  1;

    buffer_initialize(&g_child_buffer);
    if (child_initialize_signals()) {
        _exit(EXIT_FAILURE);
    }

    if (set_alarm_timer(10000)) {
        _exit(EXIT_FAILURE);
    }

    ret = child_loop(c);

    shutdown(c, SHUT_RDWR);
    close(c);

    _exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
