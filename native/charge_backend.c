#include "charge_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define USB_ONLINE_PATH "/sys/class/power_supply/usb/online"
#define BATTERY_CAPACITY_PATH "/sys/class/power_supply/battery/capacity"
#define FORCE_ACTIVE_PATH "/proc/oplus-votable/CHG_DISABLE/force_active"
#define FORCE_VALUE_PATH "/proc/oplus-votable/CHG_DISABLE/force_val"

static int read_integer(const char *path, int minimum, int maximum, int *value) {
    char buffer[32];
    char *end = NULL;
    long parsed;
    ssize_t count;
    int fd = open(path, O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        return -1;
    }
    do {
        count = read(fd, buffer, sizeof(buffer) - 1);
    } while (count < 0 && errno == EINTR);
    close(fd);
    if (count <= 0) {
        return -1;
    }
    buffer[count] = '\0';
    errno = 0;
    parsed = strtol(buffer, &end, 10);
    if (errno != 0 || end == buffer || parsed < minimum || parsed > maximum) {
        return -1;
    }
    while (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int write_boolean(const char *path, bool value) {
    const char data[] = "0\n";
    ssize_t count;
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    char output[sizeof(data)];

    if (fd < 0) {
        return -1;
    }
    output[0] = value ? '1' : '0';
    output[1] = '\n';
    output[2] = '\0';
    do {
        count = write(fd, output, 2);
    } while (count < 0 && errno == EINTR);
    if (close(fd) != 0 && count == 2) {
        return -1;
    }
    return count == 2 ? 0 : -1;
}

int charge_backend_init(void) {
    struct stat status;

    if (stat(USB_ONLINE_PATH, &status) != 0 ||
        stat(BATTERY_CAPACITY_PATH, &status) != 0 ||
        stat(FORCE_ACTIVE_PATH, &status) != 0 ||
        stat(FORCE_VALUE_PATH, &status) != 0) {
        return -1;
    }
    return 0;
}

int charge_backend_external_power_online(bool *online) {
    int value;

    if (read_integer(USB_ONLINE_PATH, 0, 1, &value) != 0) {
        return -1;
    }
    *online = value != 0;
    return 0;
}

int charge_backend_read_capacity(int *capacity) {
    return read_integer(BATTERY_CAPACITY_PATH, 0, 100, capacity);
}

int charge_backend_read_paused(bool *paused) {
    int active;
    int value;

    if (read_integer(FORCE_ACTIVE_PATH, 0, 1, &active) != 0 ||
        read_integer(FORCE_VALUE_PATH, 0, 1, &value) != 0) {
        return -1;
    }
    *paused = active == 1 && value == 1;
    return 0;
}

int charge_backend_set_paused(bool paused) {
    int active;
    int value;

    if (read_integer(FORCE_ACTIVE_PATH, 0, 1, &active) != 0 ||
        read_integer(FORCE_VALUE_PATH, 0, 1, &value) != 0) {
        return -1;
    }
    if ((paused && active == 1 && value == 1) ||
        (!paused && active == 0 && value == 0)) {
        return 0;
    }
    if (chmod(FORCE_ACTIVE_PATH, 0666) != 0 || chmod(FORCE_VALUE_PATH, 0666) != 0) {
        return -1;
    }
    if (paused) {
        if (value != 1 && write_boolean(FORCE_VALUE_PATH, true) != 0) {
            return -1;
        }
        if (active != 1 && write_boolean(FORCE_ACTIVE_PATH, true) != 0) {
            return -1;
        }
    } else {
        if (active != 0 && write_boolean(FORCE_ACTIVE_PATH, false) != 0) {
            return -1;
        }
        if (value != 0 && write_boolean(FORCE_VALUE_PATH, false) != 0) {
            return -1;
        }
    }
    return 0;
}
