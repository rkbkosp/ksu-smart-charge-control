#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_MAX_BYTES 16384
#define CONFIG_MAX_LINE_BYTES 512

static void set_error(char *error, size_t error_size, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static void set_error(char *error, size_t error_size, const char *format, ...) {
    va_list args;

    if (error == NULL || error_size == 0) {
        return;
    }
    va_start(args, format);
    (void)vsnprintf(error, error_size, format, args);
    va_end(args);
}

void config_defaults(struct charged_config *config) {
    memset(config, 0, sizeof(*config));
    config->policy.office_limit = 79;
    config->policy.office_release_minutes = 18 * 60 + 30;
    config->policy.home_limit = 100;
    config->policy.default_limit = 79;
    config->charging_reconcile_seconds = 1800;
}

static char *trim_ascii(char *text) {
    char *end;

    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        --end;
    }
    *end = '\0';
    return text;
}

static int parse_decimal(const char *text, int minimum, int maximum, int *value) {
    char *end = NULL;
    long parsed;

    if (*text == '\0') {
        return -1;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return -1;
        }
    }
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static int parse_time(const char *text, int *minutes) {
    int hour;
    int minute;

    if (strlen(text) != 5 || text[2] != ':' ||
        text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9') {
        return -1;
    }
    hour = (text[0] - '0') * 10 + text[1] - '0';
    minute = (text[3] - '0') * 10 + text[4] - '0';
    if (hour > 23 || minute > 59) {
        return -1;
    }
    *minutes = hour * 60 + minute;
    return 0;
}

static int append_ssid(struct ssid_list *list, const char *value,
                       unsigned long line_number, char *error, size_t error_size) {
    size_t length = strlen(value);

    if (length == 0 || length > CHARGED_MAX_SSID_BYTES) {
        set_error(error, error_size, "line %lu: SSID length must be 1..%d bytes",
                  line_number, CHARGED_MAX_SSID_BYTES);
        return -1;
    }
    if (list->count >= CHARGED_MAX_SSIDS) {
        set_error(error, error_size, "line %lu: too many SSIDs (maximum %d)",
                  line_number, CHARGED_MAX_SSIDS);
        return -1;
    }
    memcpy(list->values[list->count], value, length + 1);
    ++list->count;
    return 0;
}

static int parse_line(struct charged_config *config, char *line,
                      unsigned long line_number, unsigned int *seen_scalars,
                      char *error, size_t error_size) {
    enum {
        SEEN_OFFICE_LIMIT = 1U << 0,
        SEEN_RELEASE = 1U << 1,
        SEEN_HOME_LIMIT = 1U << 2,
        SEEN_DEFAULT_LIMIT = 1U << 3,
        SEEN_RECONCILE = 1U << 4
    };
    char *key;
    char *value;
    char *equals;
    unsigned int scalar_bit = 0;
    int parsed;

    key = trim_ascii(line);
    if (*key == '\0' || *key == '#') {
        return 0;
    }
    equals = strchr(key, '=');
    if (equals == NULL || strchr(equals + 1, '=') != NULL) {
        set_error(error, error_size, "line %lu: expected exactly one '='", line_number);
        return -1;
    }
    *equals = '\0';
    value = trim_ascii(equals + 1);
    key = trim_ascii(key);
    if (*key == '\0') {
        set_error(error, error_size, "line %lu: empty key", line_number);
        return -1;
    }

    if (strcmp(key, "office_ssid") == 0) {
        return append_ssid(&config->office_ssids, value, line_number, error, error_size);
    }
    if (strcmp(key, "home_ssid") == 0) {
        return append_ssid(&config->home_ssids, value, line_number, error, error_size);
    }
    if (strcmp(key, "office_limit") == 0) {
        scalar_bit = SEEN_OFFICE_LIMIT;
        if (parse_decimal(value, 1, 100, &parsed) != 0) {
            goto invalid_value;
        }
    } else if (strcmp(key, "office_release_time") == 0) {
        scalar_bit = SEEN_RELEASE;
        if (parse_time(value, &parsed) != 0) {
            goto invalid_value;
        }
    } else if (strcmp(key, "home_limit") == 0) {
        scalar_bit = SEEN_HOME_LIMIT;
        if (parse_decimal(value, 1, 100, &parsed) != 0) {
            goto invalid_value;
        }
    } else if (strcmp(key, "default_limit") == 0) {
        scalar_bit = SEEN_DEFAULT_LIMIT;
        if (parse_decimal(value, 1, 100, &parsed) != 0) {
            goto invalid_value;
        }
    } else if (strcmp(key, "charging_reconcile_seconds") == 0) {
        scalar_bit = SEEN_RECONCILE;
        if (parse_decimal(value, 0, 604800, &parsed) != 0) {
            goto invalid_value;
        }
    } else {
        set_error(error, error_size, "line %lu: unknown key '%s'", line_number, key);
        return -1;
    }

    if ((*seen_scalars & scalar_bit) != 0) {
        set_error(error, error_size, "line %lu: duplicate scalar key '%s'", line_number, key);
        return -1;
    }
    if (scalar_bit == SEEN_OFFICE_LIMIT) {
        config->policy.office_limit = parsed;
    } else if (scalar_bit == SEEN_RELEASE) {
        config->policy.office_release_minutes = parsed;
    } else if (scalar_bit == SEEN_HOME_LIMIT) {
        config->policy.home_limit = parsed;
    } else if (scalar_bit == SEEN_DEFAULT_LIMIT) {
        config->policy.default_limit = parsed;
    } else {
        config->charging_reconcile_seconds = parsed;
    }
    *seen_scalars |= scalar_bit;
    return 0;

invalid_value:
    set_error(error, error_size, "line %lu: invalid value for '%s'", line_number, key);
    return -1;
}

int config_load(const char *path, struct charged_config *config,
                char *error, size_t error_size) {
    char data[CONFIG_MAX_BYTES + 1];
    struct charged_config parsed_config;
    size_t used = 0;
    size_t line_start = 0;
    unsigned long line_number = 1;
    unsigned int seen_scalars = 0;
    int fd;

    config_defaults(&parsed_config);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        set_error(error, error_size, "open %s: %s", path, strerror(errno));
        return -1;
    }
    while (used < sizeof(data)) {
        ssize_t count = read(fd, data + used, sizeof(data) - used);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(error, error_size, "read %s: %s", path, strerror(errno));
            close(fd);
            return -1;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    close(fd);
    if (used > CONFIG_MAX_BYTES) {
        set_error(error, error_size, "config exceeds %d bytes", CONFIG_MAX_BYTES);
        return -1;
    }
    if (memchr(data, '\0', used) != NULL) {
        set_error(error, error_size, "config contains a NUL byte");
        return -1;
    }
    data[used] = '\0';

    for (size_t index = 0; index <= used; ++index) {
        if (index != used && data[index] != '\n') {
            continue;
        }
        if (index - line_start > CONFIG_MAX_LINE_BYTES) {
            set_error(error, error_size, "line %lu exceeds %d bytes",
                      line_number, CONFIG_MAX_LINE_BYTES);
            return -1;
        }
        data[index] = '\0';
        if (parse_line(&parsed_config, data + line_start, line_number,
                       &seen_scalars, error, error_size) != 0) {
            return -1;
        }
        line_start = index + 1;
        ++line_number;
    }
    *config = parsed_config;
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    return 0;
}

static bool ssid_in_list(const struct ssid_list *list, const char *ssid) {
    for (size_t index = 0; index < list->count; ++index) {
        if (strcmp(list->values[index], ssid) == 0) {
            return true;
        }
    }
    return false;
}

enum location config_classify_ssid(const struct charged_config *config,
                                   const char *ssid) {
    if (ssid_in_list(&config->home_ssids, ssid)) {
        return LOCATION_HOME;
    }
    if (ssid_in_list(&config->office_ssids, ssid)) {
        return LOCATION_OFFICE;
    }
    return LOCATION_UNKNOWN;
}
