#include "wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define WIFI_COMMAND_TIMEOUT_MS 5000
#define CONNECTED_PREFIX "Wifi is connected to "

static int64_t monotonic_milliseconds(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int set_fd_flags(int fd, int command, int flag) {
    int value = fcntl(fd, command);

    if (value < 0) {
        return -1;
    }
    return fcntl(fd, command == F_GETFD ? F_SETFD : F_SETFL, value | flag);
}

static size_t extract_ssid(const char *text, size_t length,
                           char *ssid, size_t ssid_size) {
    size_t output = 0;
    size_t index = 0;

    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\r')) {
        --length;
    }
    if (length == 0) {
        return 0;
    }
    if (text[0] != '"') {
        if (length >= ssid_size) {
            return 0;
        }
        memcpy(ssid, text, length);
        ssid[length] = '\0';
        return length;
    }

    index = 1;
    while (index < length) {
        char current = text[index++];

        if (current == '"') {
            while (index < length && (text[index] == ' ' || text[index] == '\t' ||
                                      text[index] == '\r')) {
                ++index;
            }
            if (index != length || output == 0) {
                return 0;
            }
            ssid[output] = '\0';
            return output;
        }
        if (current == '\\' && index < length &&
            (text[index] == '\\' || text[index] == '"')) {
            current = text[index++];
        }
        if (output + 1 >= ssid_size) {
            return 0;
        }
        ssid[output++] = current;
    }
    return 0;
}

enum location wifi_parse_status(const char *output, size_t length,
                                const struct charged_config *config) {
    enum location result = LOCATION_UNKNOWN;
    size_t line_start = 0;
    const size_t prefix_length = sizeof(CONNECTED_PREFIX) - 1;

    for (size_t index = 0; index <= length; ++index) {
        const char *line;
        size_t line_length;
        char ssid[CHARGED_MAX_SSID_BYTES + 1];
        enum location current;

        if (index != length && output[index] != '\n') {
            continue;
        }
        line = output + line_start;
        line_length = index - line_start;
        while (line_length > 0 && (*line == ' ' || *line == '\t')) {
            ++line;
            --line_length;
        }
        if (line_length >= prefix_length &&
            memcmp(line, CONNECTED_PREFIX, prefix_length) == 0 &&
            extract_ssid(line + prefix_length, line_length - prefix_length,
                         ssid, sizeof(ssid)) > 0) {
            current = config_classify_ssid(config, ssid);
            if (current == LOCATION_HOME) {
                return LOCATION_HOME;
            }
            if (current == LOCATION_OFFICE) {
                result = LOCATION_OFFICE;
            }
        }
        line_start = index + 1;
    }
    return result;
}

int wifi_read_location(const struct charged_config *config,
                       enum location *location) {
    char output[WIFI_STATUS_MAX_BYTES];
    size_t used = 0;
    int pipe_fds[2];
    pid_t child;
    int status = 0;
    bool eof = false;
    int64_t deadline;

    *location = LOCATION_UNKNOWN;
    if (pipe(pipe_fds) != 0) {
        return -1;
    }
    if (set_fd_flags(pipe_fds[0], F_GETFD, FD_CLOEXEC) != 0 ||
        set_fd_flags(pipe_fds[1], F_GETFD, FD_CLOEXEC) != 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }

    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (child == 0) {
        char *const argv[] = {"cmd", "wifi", "status", NULL};
        char *const envp[] = {"PATH=/system/bin:/system/xbin", "ANDROID_ROOT=/system",
                              "ANDROID_DATA=/data", NULL};
        int null_fd;

        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        null_fd = open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDERR_FILENO);
        }
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execve("/system/bin/cmd", argv, envp);
        _exit(127);
    }

    close(pipe_fds[1]);
    deadline = monotonic_milliseconds();
    if (deadline < 0) {
        goto fail;
    }
    deadline += WIFI_COMMAND_TIMEOUT_MS;
    while (!eof) {
        struct pollfd poll_fd = {.fd = pipe_fds[0], .events = POLLIN | POLLHUP};
        int64_t now = monotonic_milliseconds();
        int timeout;
        int poll_result;

        if (now < 0 || now >= deadline) {
            goto fail;
        }
        timeout = (int)(deadline - now);
        poll_result = poll(&poll_fd, 1, timeout);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto fail;
        }
        if (poll_result == 0) {
            goto fail;
        }
        if ((poll_fd.revents & (POLLIN | POLLHUP)) != 0) {
            char chunk[1024];
            ssize_t count = read(pipe_fds[0], chunk, sizeof(chunk));

            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                goto fail;
            }
            if (count == 0) {
                eof = true;
            } else if (used < sizeof(output)) {
                size_t copy = (size_t)count;
                if (copy > sizeof(output) - used) {
                    copy = sizeof(output) - used;
                }
                memcpy(output + used, chunk, copy);
                used += copy;
            }
        }
        if ((poll_fd.revents & (POLLERR | POLLNVAL)) != 0) {
            goto fail;
        }
    }
    close(pipe_fds[0]);
    pipe_fds[0] = -1;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return -1;
    }
    *location = wifi_parse_status(output, used, config);
    return 0;

fail:
    close(pipe_fds[0]);
    (void)kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return -1;
}
