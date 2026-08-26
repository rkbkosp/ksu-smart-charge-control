#ifndef CHARGED_WIFI_H
#define CHARGED_WIFI_H

#include <stddef.h>

#include "config.h"

#define WIFI_STATUS_MAX_BYTES 8192

enum location wifi_parse_status(const char *output, size_t length,
                                const struct charged_config *config);
int wifi_read_location(const struct charged_config *config,
                       enum location *location);

#endif
