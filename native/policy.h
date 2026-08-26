#ifndef CHARGED_POLICY_H
#define CHARGED_POLICY_H

enum location {
    LOCATION_HOME,
    LOCATION_OFFICE,
    LOCATION_UNKNOWN
};

struct policy_settings {
    int office_limit;
    int office_release_minutes;
    int home_limit;
    int default_limit;
};

int policy_limit(enum location location, int minutes_since_midnight,
                 const struct policy_settings *settings);

#endif
