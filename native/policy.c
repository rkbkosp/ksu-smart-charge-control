#include "policy.h"

int policy_limit(enum location location, int minutes_since_midnight,
                 const struct policy_settings *settings) {
    switch (location) {
    case LOCATION_HOME:
        return settings->home_limit;
    case LOCATION_OFFICE:
        if (minutes_since_midnight >= settings->office_release_minutes) {
            return 100;
        }
        return settings->office_limit;
    case LOCATION_UNKNOWN:
    default:
        return settings->default_limit;
    }
}
