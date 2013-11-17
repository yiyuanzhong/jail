#include <sys/mount.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "cmdline.h"
#include "config.h"
#include "utility.h"

#include "jaild.h"

typedef struct {
    const char *path;
    int s;
} context_t;

uid_t       g_args_uid;
gid_t       g_args_gid;
const char *g_args_name;
const char *g_args_shell;

static const char *g_args_root;
static const char *g_args_socket;
static array_t     g_args_mounts;

static volatile sig_atomic_t g_server_sigchld;
static volatile sig_atomic_t g_server_sigterm;

static int g_server_clone;
static int g_server_daemon;

static void server_sigchld_handler(int signum)
{
    g_server_sigchld = 1;
}

static void server_sigterm_handler(int signum)
{
    g_server_sigterm = 1;
}

static int server_initialize_signals_first(void)
{
    struct sigaction act;
    sigset_t set;
    int i;

    /* Ignore every signal. */
    memset(&act, 0, sizeof(act));
    if (sigemptyset(&act.sa_mask)) {
        return -1;
    }

    act.sa_handler = SIG_IGN;
    for (i = 1; i <= NSIG; ++i) {
        if (sigaction(i, &act, NULL)) {
            if (errno != EINVAL) {
                return -1;
            }
        }
    }

    /* Blocks useful signals. */
    if (sigemptyset(&set)                       ||
        sigaddset(&set, SIGCHLD)                ||
        sigaddset(&set, SIGHUP)                 ||
        sigaddset(&set, SIGINT)                 ||
        sigaddset(&set, SIGQUIT)                ||
        sigaddset(&set, SIGTERM)                ||
        sigprocmask(SIG_SETMASK, &set, NULL)    ){

        return -1;
    }

    /* And capture some. */
    act.sa_handler = server_sigterm_handler;
    if (sigaction(SIGHUP,  &act, NULL) ||
        sigaction(SIGINT,  &act, NULL) ||
        sigaction(SIGQUIT, &act, NULL) ||
        sigaction(SIGTERM, &act, NULL) ){

        return -1;
    }

    act.sa_handler = server_sigchld_handler;
    if (sigaction(SIGCHLD, &act, NULL)) {
        return -1;
    }

    return 0;
}

static int server_initialize_signals_second(void)
{
    struct sigaction act;
    sigset_t set;

    /* Ignore unused signals. */
    act.sa_handler = SIG_IGN;
    if (sigaction(SIGQUIT, &act, NULL) || sigaction(SIGTERM, &act, NULL)) {
        return -1;
    }

    /* Unblocks those. */
    if (sigemptyset(&set)                       ||
        sigaddset(&set, SIGQUIT)                ||
        sigaddset(&set, SIGTERM)                ||
        sigprocmask(SIG_UNBLOCK, &set, NULL)    ){

        return -1;
    }

    return 0;
}

static int server_start(const char *path)
{
    struct sockaddr_un addr;
    socklen_t len;
    int s;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    len = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (len < 0) {
        return -1;
    } else if (len >= sizeof(addr.sun_path)) {
        len = sizeof(struct sockaddr_un) - 1;
    } else {
        len += offsetof(struct sockaddr_un, sun_path);
    }

    s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) {
        return -1;
    }

    if (set_non_blocking_mode(s)                ||
        bind(s, (struct sockaddr *)&addr, len)  ){

        close(s);
        return -1;
    }

    /* You can't use fchown(2) on a socket. */
    if (chown(path, g_args_uid, g_args_gid) ||
        chmod(path, S_IRUSR | S_IWUSR)      ||
        listen(s, 10)                       ){

        close(s);
        unlink(path);
        return -1;
    }

    return s;
}

static int server_stop(int s, const char *path)
{
    int ret;
    ret = 0;
    ret |= close(s);
    ret |= unlink(path);
    return ret == 0 ? 0 : -1;
}

static int server_sigchld(void)
{
    int status;
    pid_t pid;
    int ret;

    if (!g_server_sigchld) {
        return 0;
    }

    ret = 0;
    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            ++ret;
            continue;
        }

        if (pid < 0) {
            if (errno != ECHILD) {
                return -1;
            }
        }

        break;
    }

    g_server_sigchld = 0;
    return ret;
}

static int server_loop(int s)
{
    struct timespec tv;
    sigset_t set;
    fd_set fds;
    pid_t pid;
    int ret;
    int c;

    FD_ZERO(&fds);
    sigemptyset(&set);
    while (!g_server_sigterm) {
        if (server_sigchld() < 0) {
            return -1;
        }

        /* In case that pselect(2) is emulated by glibc. */
        tv.tv_sec = 5;
        tv.tv_nsec = 0;
        FD_SET(s, &fds);
        ret = pselect(s + 1, &fds, NULL, NULL, &tv, &set);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        } else if (ret == 0) {
            continue;
        }

        c = accept(s, NULL, NULL);
        if (c < 0) { /* Failed: just continue. */
            continue;
        }

        pid = fork();
        if (pid == 0) { /* Failed or server: just continue. */
            close(s);
            child_main(c);
        }

        close(c);
    }

    return 0;
}

static int server_mount_dev(void)
{
    char path[PATH_MAX];
    int ret;

    ret = snprintf(path, sizeof(path), "%s/dev", g_args_root);
    if (ret < 0 || (size_t)ret >= sizeof(path)) {
        return -1;
    }

    if (mount("/dev", path, NULL, MS_BIND, NULL)) {
        return -1;
    }

    return 0;
}

static int server_daemonize_chroot(void)
{
    assert(g_server_clone);
    assert(g_args_root);

    if (server_mount_dev()) {
        return -1;
    }

    if (chdir(g_args_root) || chroot(g_args_root)) {
        return -1;
    }

    umount("/dev/pts");
    umount("/proc");
    umount("/sys");

    if (mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC           , NULL) ||
        mount("proc",   "/proc",    "proc",   MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) ||
        mount("sysfs",  "/sys",     "sysfs",  MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) ){

        return -1;
    }

    return 0;
}

static int server_daemonize_second(void)
{
    int s;

    if (setsid() < 0) {
        return -1;
    }

    s = open("/dev/null", O_NOCTTY | O_RDWR);
    if (s < 0) {
        return -1;
    }

    dup2(s, STDIN_FILENO);
    dup2(s, STDOUT_FILENO);
    dup2(s, STDERR_FILENO);
    if (s != STDIN_FILENO && s != STDOUT_FILENO && s != STDERR_FILENO) {
        close(s);
    }

    return 0;
}

static int server_bind_mount(char *entry)
{
    char buffer[PATH_MAX];
    char *rpath;
    char *jpath;
    int ret;

    jpath = entry;
    if (*jpath != '/') {
        return -1;
    }

    rpath = strchr(entry, '=');
    if (!rpath) {
        return -1;
    }

    *rpath++ = '\0';
    if (*rpath != '/') {
        return -1;
    }

    if (g_args_root) {
        ret = snprintf(buffer, sizeof(buffer), "%s/%s", g_args_root, jpath);
        if (ret < 0 || ret >= sizeof(buffer)) {
            return -1;
        }

        jpath = buffer;
    }

    return mount(rpath, jpath, NULL, MS_BIND, NULL);
}

static int server_daemonize_bind_mount(void)
{
    size_t count;
    char *entry;
    size_t i;

    count = array_get_count(&g_args_mounts);
    for (i = 0; i < count; ++i) {
        entry = array_get_element(&g_args_mounts, i);
        if (server_bind_mount(entry)) {
            return -1;
        }
    }

    return 0;
}

static int server_daemonize_third(void)
{
    if (setsid() < 0) {
        return -1;
    }

    if (g_server_clone && !g_args_root) {
        umount("/proc");
        if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL)) {
            return -1;
        }
    }

    if (server_daemonize_bind_mount()) {
        return -1;
    }

    if (g_args_root) {
        if (server_daemonize_chroot()) {
            return -1;
        }
    }

    return 0;
}

static int server_daemonize_first(void)
{
    int i;

    for (i = 3; i < 64; ++i) {
        close(i);
    }

    if (chdir("/")) {
        return -1;
    }

    return 0;
}

static int server_main(void *arg)
{
    context_t *context;
    int ret;

    context = (context_t *)arg;

    cmdline_set_process_name("master [%d:%d]", g_args_uid, g_args_gid);

    if (server_initialize_signals_second()) {
        close(context->s);
        _exit(EXIT_FAILURE);
    }

    if (server_daemonize_third()){
        close(context->s);
        _exit(EXIT_FAILURE);
    }

    ret = server_loop(context->s);

    close(context->s);
    _exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

static pid_t server_fork_fork(context_t *context)
{
    pid_t pid;

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        _exit(server_main(context));
    }

    return pid;
}

static pid_t server_fork_clone(context_t *context)
{
    char buffer[1024 * 1024 * 4];
    pid_t pid;
    int flags;

    flags  = SIGCHLD;
    flags |= CLONE_NEWIPC;
    flags |= CLONE_NEWNS;
    flags |= CLONE_NEWPID;
    flags |= CLONE_NEWUTS;

    /* We don't have IP to waste. */
    /* flags |= CLONE_NEWNET; */

    pid = clone(server_main, buffer + sizeof(buffer), flags, context);
    if (pid < 0) {
        return -1;
    }

    return pid;
}

static pid_t server_fork(context_t *context)
{
    pid_t pid;

    g_server_clone = 1;
    pid = server_fork_clone(context);
    if (pid > 0) {
        return pid;
    }

    if (g_args_root || array_get_count(&g_args_mounts)) {
        /* Oops, not possible. */
        return -1;
    }

    g_server_clone = 0;
    pid = server_fork_fork(context);
    if (pid > 0) {
        return pid;
    }

    return -1;
}

static void print_usage(const char *argv, int error)
{
    FILE *fp;

    fp = error ? stderr : stdout;
    fprintf(fp, "%s [-a socket] [-u uid] [-g gid] [-n name] [-r root] "
                "[-s shell] [-m jpath=rpath]...\n", argv);

    fprintf(fp, "%s -h\n", argv);
    fprintf(fp, "%s -v\n", argv);
}

static void print_version(void)
{
    printf("1.0.0.0\n");
}

static int parse_args(int argc, char *argv[])
{
    char path[PATH_MAX];
    int opt;
    int ret;

    g_args_name = NULL;
    g_args_shell = NULL;
    g_args_uid = geteuid();
    g_args_gid = getegid();

    g_args_root = NULL;
    g_args_socket = kSocketPrefix;
    array_initialize(&g_args_mounts);

    while ((opt = getopt(argc, argv, "cu:g:r:s:a:n:m:hv")) != -1) {
        switch (opt) {
        case 'c':
            g_server_daemon = 0;
            break;

        case 'u':
            g_args_uid = atoi32(optarg);
            if (g_args_gid <= 0) {
                g_args_gid = g_args_uid;
            }
            break;

        case 'g':
            g_args_gid = atoi32(optarg);
            break;

        case 'n':
            g_args_name = optarg;
            break;

        case 'r':
            g_args_root = optarg;
            break;

        case 's':
            g_args_shell = optarg;
            break;

        case 'a':
            g_args_socket = optarg;
            break;

        case 'm':
            array_push(&g_args_mounts, optarg);
            break;

        case 'h':
            print_usage(argv[0], 0);
            return 1;

        case 'v':
            print_version();
            return 1;

        default:
            print_usage(argv[0], 1);
            return -1;
        };
    }

    if (optind < argc   ||
        g_args_uid <= 0 ||
        g_args_gid <= 0 ){

        print_usage(argv[0], 1);
        return -1;
    }

    ret = snprintf(path, sizeof(path), "%s-%d-%d.sock", g_args_socket, g_args_uid, g_args_gid);
    if (ret < 0 || (size_t)ret >= sizeof(path)) {
        return -1;
    }

    g_args_socket = strdup(path);
    if (!g_args_socket) {
        return -1;
    }

    return 0;
}

static int server_wait_for_child(pid_t pid)
{
    sigset_t set;
    int status;
    pid_t p;
    int ret;

    if (sigemptyset(&set)) {
        kill(pid, SIGKILL);
        kill(pid, SIGCONT);
        return -1;
    }

    for (;;) {
        if (g_server_sigterm) {
            g_server_sigterm = 0;
            kill(pid, SIGINT);  /* SIGINT is for halt. */
            kill(pid, SIGCONT); /* Just in case. */
        }

        p = waitpid(-1, &status, WNOHANG);
        if (p < 0) {
            kill(pid, SIGKILL);
            kill(pid, SIGCONT);
            return -1;
        } else if (p == pid) {
            break;
        }

        ret = sigsuspend(&set);
        if (ret < 0) {
            if (errno != EINTR) {
                kill(pid, SIGKILL);
                kill(pid, SIGCONT);
                return -1;
            }
        }
    }

    return status;
}

int main(int argc, char *argv[])
{
    context_t context;
    pid_t pid;
    int ret;

    argv = cmdline_setup(argc, argv);
    if (!argv) {
        return EXIT_FAILURE;
    }

    umask(S_IRWXG | S_IRWXO); /* 0077 */
    g_server_sigchld = 0;
    g_server_sigterm = 0;
    g_server_daemon = 1;

    ret = parse_args(argc, argv);
    if (ret < 0) {
        return EXIT_FAILURE;
    } else if (ret > 0) {
        return EXIT_SUCCESS;
    }

    if (server_initialize_signals_first()) {
        return EXIT_FAILURE;
    }

    if (server_daemonize_first()) {
        return EXIT_FAILURE;
    }

    context.path = g_args_socket;
    context.s = server_start(g_args_socket);
    if (context.s < 0) {
        return EXIT_FAILURE;
    }

    if (g_server_daemon) {
        pid = fork();
        if (pid < 0) {
            server_stop(context.s, context.path);
            return EXIT_FAILURE;
        } else if (pid > 0) {
            _exit(EXIT_SUCCESS);
        }

        if (server_daemonize_second()) {
            server_stop(context.s, context.path);
            _exit(EXIT_FAILURE);
        }
    }

    pid = server_fork(&context);
    if (pid < 0) {
        server_stop(context.s, context.path);
        _exit(EXIT_FAILURE);
    }

    cmdline_set_process_name("babysitter [%d:%d]", g_args_uid, g_args_gid);

    close(context.s);
    ret = server_wait_for_child(pid);
    if (ret < 0 || !WIFEXITED(ret) || WEXITSTATUS(ret) != 0) {
        unlink(context.path);
        _exit(EXIT_FAILURE);
    }

    unlink(context.path);
    _exit(EXIT_SUCCESS);
}
