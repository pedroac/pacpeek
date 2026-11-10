#define _GNU_SOURCE
#include "query.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static pid_t child = -1;
static int pipe_fd = -1;
static char output[512];
static size_t output_len;

static const char *script =
    "export LC_ALL=C\n"
    "printf 'out '; wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null || echo\n"
    "printf 'in '; wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null || echo\n"
    "if n=$(makoctl list 2>/dev/null); then\n"
    "  printf 'notifications %s\\n' \"$(printf '%s\\n' \"$n\" | grep -c '^Notification [0-9][0-9]*: ')\"\n"
    "elif n=$(dunstctl count displayed 2>/dev/null); then\n"
    "  printf 'notifications %s\\n' \"$n\"\n"
    "fi\n";

int query_start(void) {
    if (pipe_fd >= 0) return pipe_fd;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) return -1;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    /* Its own process group, so cancelling kills wpctl and friends along with the shell. */
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    char *argv[] = { "sh", "-c", (char *)script, NULL };
    int err = posix_spawn(&child, "/bin/sh", &actions, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(fds[1]);

    if (err != 0) {
        close(fds[0]);
        child = -1;
        return -1;
    }

    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    pipe_fd = fds[0];
    output_len = 0;
    return pipe_fd;
}

static void parse_volume(const char *line, Volume *vol) {
    double value;
    vol->valid = sscanf(line, "Volume: %lf", &value) == 1 && value >= 0 && value <= 10;
    if (!vol->valid) return;
    vol->percent = (int)(value * 100 + 0.5);
    vol->muted = strstr(line, "[MUTED]") != NULL;
}

static void finish(void) {
    close(pipe_fd);
    pipe_fd = -1;
    waitpid(child, NULL, 0);
    child = -1;
}

bool query_read(QueryState *state) {
    if (pipe_fd < 0) return true;

    for (;;) {
        ssize_t n = read(pipe_fd, output + output_len, sizeof(output) - 1 - output_len);
        if (n > 0) {
            output_len += n;
            if (output_len < sizeof(output) - 1) continue;
        } else if (n < 0 && errno == EAGAIN) {
            return false;
        }
        break;
    }

    finish();
    output[output_len] = '\0';

    state->notifications = -1;
    for (char *line = strtok(output, "\n"); line; line = strtok(NULL, "\n")) {
        if (strncmp(line, "out ", 4) == 0) parse_volume(line + 4, &state->output);
        else if (strncmp(line, "in ", 3) == 0) parse_volume(line + 3, &state->input);
        else sscanf(line, "notifications %d", &state->notifications);
    }
    return true;
}

void query_cancel(void) {
    if (pipe_fd < 0) return;
    kill(-child, SIGKILL);
    finish();
}
