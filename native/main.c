#include "charge_backend.h"
#include "config.h"
#include "policy.h"
#include "power_event.h"
#include "wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define LOCK_PATH "/data/adb/charged.lock"

struct daemon_state {
    struct charged_config config;
    enum location location;
    bool external_power;
    bool paused;
    bool paused_known;
    bool backend_failed;
    int desired_limit;
    time_t next_reconcile;
};

static void close_inherited_fds(void) {
#ifdef __NR_close_range
    if (syscall(__NR_close_range, 3U, UINT_MAX, 0U) == 0) {
        return;
    }
#endif
    long maximum = sysconf(_SC_OPEN_MAX);

    if (maximum < 0 || maximum > 4096) {
        maximum = 4096;
    }
    for (long fd = 3; fd < maximum; ++fd) {
        (void)close((int)fd);
    }
}

static const char *location_name(enum location location) {
    switch (location) {
    case LOCATION_HOME:
        return "HOME";
    case LOCATION_OFFICE:
        return "OFFICE";
    case LOCATION_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

static int lock_single_instance(void) {
    int fd = open(LOCK_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    if (fd < 0) {
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return errno == EWOULDBLOCK || errno == EAGAIN ? -2 : -1;
    }
    return fd;
}

static int setup_signal_fd(void) {
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        return -1;
    }
    return signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
}

static int local_minutes(time_t now, int *minutes) {
    struct tm local;

    if (localtime_r(&now, &local) == NULL) {
        return -1;
    }
    *minutes = local.tm_hour * 60 + local.tm_min;
    return 0;
}

static time_t next_policy_boundary(const struct daemon_state *state, time_t now) {
    struct tm local;
    int minutes;

    if (state->location != LOCATION_OFFICE || localtime_r(&now, &local) == NULL) {
        return 0;
    }
    minutes = local.tm_hour * 60 + local.tm_min;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    if (minutes < state->config.policy.office_release_minutes) {
        local.tm_hour = state->config.policy.office_release_minutes / 60;
        local.tm_min = state->config.policy.office_release_minutes % 60;
    } else {
        ++local.tm_mday;
        local.tm_hour = 0;
        local.tm_min = 0;
    }
    return mktime(&local);
}

static int arm_timer(int timer_fd, const struct daemon_state *state) {
    struct itimerspec timer;
    time_t now;
    time_t boundary;
    time_t next;

    memset(&timer, 0, sizeof(timer));
    if (!state->external_power) {
        return timerfd_settime(timer_fd, 0, &timer, NULL);
    }
    now = time(NULL);
    if (now == (time_t)-1) {
        return -1;
    }
    boundary = next_policy_boundary(state, now);
    next = boundary;
    if (state->next_reconcile > 0 && (next == 0 || state->next_reconcile < next)) {
        next = state->next_reconcile;
    }
    if (next == 0) {
        return timerfd_settime(timer_fd, 0, &timer, NULL);
    }
    timer.it_value.tv_sec = next;
    return timerfd_settime(timer_fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
                           &timer, NULL);
}

static void schedule_reconcile(struct daemon_state *state, time_t now) {
    if (state->external_power && state->config.charging_reconcile_seconds > 0) {
        state->next_reconcile = now + state->config.charging_reconcile_seconds;
    } else {
        state->next_reconcile = 0;
    }
}

static void refresh_location(struct daemon_state *state) {
    enum location new_location = LOCATION_UNKNOWN;

    if (wifi_read_location(&state->config, &new_location) != 0) {
        dprintf(STDERR_FILENO, "charged: SSID command failed; using UNKNOWN\n");
        new_location = LOCATION_UNKNOWN;
    }
    if (new_location != state->location) {
        dprintf(STDERR_FILENO, "charged: location %s -> %s\n",
                location_name(state->location), location_name(new_location));
        state->location = new_location;
    }
}

static void sync_charge_state(struct daemon_state *state, bool allow_retry) {
    bool should_pause;
    int capacity;

    if (!state->external_power || state->desired_limit < 0 ||
        (state->backend_failed && !allow_retry)) {
        return;
    }
    if (charge_backend_read_capacity(&capacity) != 0) {
        if (!state->backend_failed) {
            dprintf(STDERR_FILENO, "charged: charge backend capacity read failed\n");
        }
        state->backend_failed = true;
        return;
    }
    should_pause = state->desired_limit < 100 && capacity >= state->desired_limit;
    if (state->paused_known && should_pause == state->paused) {
        return;
    }
    if (state->backend_failed && !allow_retry) {
        return;
    }
    if (charge_backend_set_paused(should_pause) != 0) {
        dprintf(STDERR_FILENO, "charged: charge backend write failed\n");
        state->backend_failed = true;
        return;
    }
    state->paused = should_pause;
    state->paused_known = true;
    state->backend_failed = false;
    dprintf(STDERR_FILENO, "charged: charging %s at capacity %d (limit %d)\n",
            should_pause ? "paused" : "resumed", capacity, state->desired_limit);
}

static void evaluate_and_apply(struct daemon_state *state, bool allow_retry) {
    time_t now = time(NULL);
    int minutes;
    int desired;

    if (now == (time_t)-1 || local_minutes(now, &minutes) != 0) {
        dprintf(STDERR_FILENO, "charged: local time read failed\n");
        return;
    }
    desired = policy_limit(state->location, minutes, &state->config.policy);
    if (desired != state->desired_limit) {
        dprintf(STDERR_FILENO, "charged: desired limit %d -> %d\n",
                state->desired_limit, desired);
        state->desired_limit = desired;
        state->backend_failed = false;
        allow_retry = true;
    }
    sync_charge_state(state, allow_retry);
}

static int reload_config(const char *path, struct daemon_state *state) {
    struct charged_config updated;
    char error[256];

    if (config_load(path, &updated, error, sizeof(error)) != 0) {
        dprintf(STDERR_FILENO, "charged: config reload failed: %s\n", error);
        return -1;
    }
    state->config = updated;
    refresh_location(state);
    schedule_reconcile(state, time(NULL));
    state->backend_failed = false;
    evaluate_and_apply(state, true);
    return 0;
}

static void usage(const char *program) {
    dprintf(STDERR_FILENO, "usage: %s --config PATH\n", program);
}

int main(int argc, char **argv) {
    const char *config_path = NULL;
    struct daemon_state state;
    struct pollfd poll_fds[3];
    char config_error[256];
    int lock_fd;
    int signal_fd;
    int timer_fd;
    int power_fd;
    bool running = true;

    if (argc == 3 && strcmp(argv[1], "--config") == 0) {
        config_path = argv[2];
    } else {
        usage(argv[0]);
        return 2;
    }

    close_inherited_fds();
    memset(&state, 0, sizeof(state));
    state.location = LOCATION_UNKNOWN;
    state.desired_limit = -1;
    if (config_load(config_path, &state.config, config_error, sizeof(config_error)) != 0) {
        dprintf(STDERR_FILENO, "charged: config error: %s\n", config_error);
        return 1;
    }
    lock_fd = lock_single_instance();
    if (lock_fd == -2) {
        return 0;
    }
    if (lock_fd < 0) {
        dprintf(STDERR_FILENO, "charged: lock failed: %s\n", strerror(errno));
        return 1;
    }
    if (charge_backend_init() != 0 ||
        charge_backend_external_power_online(&state.external_power) != 0) {
        dprintf(STDERR_FILENO, "charged: charge backend initialization failed\n");
        close(lock_fd);
        return 1;
    }
    if (charge_backend_read_paused(&state.paused) == 0) {
        state.paused_known = true;
    }
    signal_fd = setup_signal_fd();
    timer_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    power_fd = power_event_open();
    if (signal_fd < 0 || timer_fd < 0 || power_fd < 0) {
        dprintf(STDERR_FILENO, "charged: event fd initialization failed: %s\n", strerror(errno));
        if (signal_fd >= 0) close(signal_fd);
        if (timer_fd >= 0) close(timer_fd);
        if (power_fd >= 0) close(power_fd);
        close(lock_fd);
        return 1;
    }

    dprintf(STDERR_FILENO, "charged: startup\n");
    refresh_location(&state);
    schedule_reconcile(&state, time(NULL));
    evaluate_and_apply(&state, true);

    poll_fds[0] = (struct pollfd){.fd = signal_fd, .events = POLLIN};
    poll_fds[1] = (struct pollfd){.fd = timer_fd, .events = POLLIN};
    poll_fds[2] = (struct pollfd){.fd = power_fd, .events = POLLIN};
    while (running) {
        int poll_result;

        if (arm_timer(timer_fd, &state) != 0) {
            dprintf(STDERR_FILENO, "charged: timer arm failed: %s\n", strerror(errno));
            break;
        }
        poll_result = poll(poll_fds, 3, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            dprintf(STDERR_FILENO, "charged: poll failed: %s\n", strerror(errno));
            break;
        }
        if ((poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
            (poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
            (poll_fds[2].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            dprintf(STDERR_FILENO, "charged: event fd failure\n");
            break;
        }

        if ((poll_fds[0].revents & POLLIN) != 0) {
            struct signalfd_siginfo signal_info;
            ssize_t count;

            while ((count = read(signal_fd, &signal_info, sizeof(signal_info))) ==
                   (ssize_t)sizeof(signal_info)) {
                if (signal_info.ssi_signo == SIGINT || signal_info.ssi_signo == SIGTERM) {
                    running = false;
                } else if (signal_info.ssi_signo == SIGHUP) {
                    (void)reload_config(config_path, &state);
                }
            }
            if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                running = false;
            }
        }

        if (running && (poll_fds[2].revents & POLLIN) != 0) {
            bool saw_power_supply;

            if (power_event_drain(power_fd, &saw_power_supply) != 0) {
                dprintf(STDERR_FILENO, "charged: power uevent receive failed\n");
                running = false;
            } else if (saw_power_supply) {
                bool online;

                if (charge_backend_external_power_online(&online) != 0) {
                    dprintf(STDERR_FILENO, "charged: external power read failed\n");
                } else if (online != state.external_power) {
                    bool was_online = state.external_power;
                    state.external_power = online;
                    state.backend_failed = false;
                    if (!was_online && online) {
                        refresh_location(&state);
                        schedule_reconcile(&state, time(NULL));
                        evaluate_and_apply(&state, true);
                    } else {
                        state.next_reconcile = 0;
                    }
                } else if (online) {
                    sync_charge_state(&state, false);
                }
            }
        }

        if (running && (poll_fds[1].revents & POLLIN) != 0) {
            uint64_t expirations;
            ssize_t count = read(timer_fd, &expirations, sizeof(expirations));
            bool clock_changed = count < 0 && errno == ECANCELED;
            time_t now = time(NULL);

            if (count < 0 && errno != EAGAIN && !clock_changed) {
                dprintf(STDERR_FILENO, "charged: timer read failed: %s\n", strerror(errno));
                running = false;
            } else if (state.external_power) {
                bool reconcile_due = state.next_reconcile > 0 && now >= state.next_reconcile;

                if (reconcile_due) {
                    refresh_location(&state);
                }
                if (reconcile_due || clock_changed) {
                    schedule_reconcile(&state, now);
                }
                state.backend_failed = false;
                evaluate_and_apply(&state, true);
            }
        }
    }

    if (charge_backend_set_paused(false) != 0) {
        dprintf(STDERR_FILENO, "charged: failed to resume charging during shutdown\n");
    }
    close(power_fd);
    close(timer_fd);
    close(signal_fd);
    close(lock_fd);
    return running ? 1 : 0;
}
