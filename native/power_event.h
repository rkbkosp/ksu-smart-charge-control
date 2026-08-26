#ifndef CHARGED_POWER_EVENT_H
#define CHARGED_POWER_EVENT_H

#include <stdbool.h>
#include <stddef.h>

int power_event_open(void);
bool power_event_is_power_supply(const char *message, size_t length);
int power_event_drain(int fd, bool *saw_power_supply);

#endif
