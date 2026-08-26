#ifndef CHARGED_CHARGE_BACKEND_H
#define CHARGED_CHARGE_BACKEND_H

#include <stdbool.h>

int charge_backend_init(void);
int charge_backend_external_power_online(bool *online);
int charge_backend_read_capacity(int *capacity);
int charge_backend_read_paused(bool *paused);
int charge_backend_set_paused(bool paused);

#endif
