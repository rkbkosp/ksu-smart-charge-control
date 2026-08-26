#ifndef CHARGED_CONFIG_H
#define CHARGED_CONFIG_H

#include <stddef.h>

#include "policy.h"

#define CHARGED_MAX_SSIDS 16
#define CHARGED_MAX_SSID_BYTES 128

struct ssid_list {
    char values[CHARGED_MAX_SSIDS][CHARGED_MAX_SSID_BYTES + 1];
    size_t count;
};

struct charged_config {
    struct ssid_list office_ssids;
    struct ssid_list home_ssids;
    struct policy_settings policy;
    int charging_reconcile_seconds;
};

void config_defaults(struct charged_config *config);
int config_load(const char *path, struct charged_config *config,
                char *error, size_t error_size);
enum location config_classify_ssid(const struct charged_config *config,
                                   const char *ssid);

#endif
