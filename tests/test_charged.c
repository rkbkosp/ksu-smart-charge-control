#include "config.h"
#include "policy.h"
#include "wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int checks;

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void test_policy(void) {
    struct policy_settings settings = {
        .office_limit = 79,
        .office_release_minutes = 18 * 60 + 30,
        .home_limit = 100,
        .default_limit = 79,
    };

    CHECK(policy_limit(LOCATION_HOME, 9 * 60, &settings) == 100);
    CHECK(policy_limit(LOCATION_HOME, 23 * 60, &settings) == 100);
    CHECK(policy_limit(LOCATION_OFFICE, 18 * 60 + 29, &settings) == 79);
    CHECK(policy_limit(LOCATION_OFFICE, 18 * 60 + 30, &settings) == 100);
    CHECK(policy_limit(LOCATION_OFFICE, 23 * 60 + 59, &settings) == 100);
    CHECK(policy_limit(LOCATION_OFFICE, 0, &settings) == 79);
    CHECK(policy_limit(LOCATION_UNKNOWN, 12 * 60, &settings) == 79);

    settings.default_limit = 65;
    CHECK(policy_limit(LOCATION_UNKNOWN, 12 * 60, &settings) == 65);
}

static int load_text_config(const char *text, struct charged_config *config,
                            char *error, size_t error_size) {
    char path[] = "/tmp/charged-config-XXXXXX";
    size_t length = strlen(text);
    size_t written = 0;
    int fd = mkstemp(path);
    int result;

    CHECK(fd >= 0);
    while (written < length) {
        ssize_t count = write(fd, text + written, length - written);
        CHECK(count > 0);
        written += (size_t)count;
    }
    CHECK(close(fd) == 0);
    result = config_load(path, config, error, error_size);
    CHECK(unlink(path) == 0);
    return result;
}

static void test_config(void) {
    static const char valid[] =
        "# repeated SSIDs are intentional\n"
        "office_ssid=Company-WiFi\n"
        "office_ssid=办公室网络\n"
        "home_ssid=Home\n"
        "home_ssid=家庭网络-5G\n"
        "office_limit=79\n"
        "office_release_time=18:30\n"
        "home_limit=100\n"
        "default_limit=77\n"
        "charging_reconcile_seconds=1800\n";
    struct charged_config config;
    char error[256];

    CHECK(load_text_config(valid, &config, error, sizeof(error)) == 0);
    CHECK(config.office_ssids.count == 2);
    CHECK(config.home_ssids.count == 2);
    CHECK(config.policy.office_limit == 79);
    CHECK(config.policy.office_release_minutes == 18 * 60 + 30);
    CHECK(config.policy.home_limit == 100);
    CHECK(config.policy.default_limit == 77);
    CHECK(config.charging_reconcile_seconds == 1800);
    CHECK(config_classify_ssid(&config, "Company-WiFi") == LOCATION_OFFICE);
    CHECK(config_classify_ssid(&config, "办公室网络") == LOCATION_OFFICE);
    CHECK(config_classify_ssid(&config, "家庭网络-5G") == LOCATION_HOME);
    CHECK(config_classify_ssid(&config, "") == LOCATION_UNKNOWN);

    CHECK(load_text_config("home_ssid=\n", &config, error, sizeof(error)) != 0);
    CHECK(strstr(error, "SSID length") != NULL);
    CHECK(load_text_config("office_limit=0\n", &config, error, sizeof(error)) != 0);
    CHECK(strstr(error, "invalid value") != NULL);
    CHECK(load_text_config("home_limit=100\nhome_limit=99\n",
                           &config, error, sizeof(error)) != 0);
    CHECK(strstr(error, "duplicate scalar") != NULL);
    CHECK(load_text_config("mystery_key=1\n", &config, error, sizeof(error)) != 0);
    CHECK(strstr(error, "unknown key") != NULL);
    CHECK(load_text_config("office_release_time=24:00\n",
                           &config, error, sizeof(error)) != 0);
    CHECK(load_text_config("charging_reconcile_seconds=0\n",
                           &config, error, sizeof(error)) == 0);
    CHECK(config.charging_reconcile_seconds == 0);
}

static void set_wifi_config(struct charged_config *config) {
    config_defaults(config);
    config->office_ssids.count = 2;
    strcpy(config->office_ssids.values[0], "Company-WiFi");
    strcpy(config->office_ssids.values[1], "办公室网络");
    config->home_ssids.count = 2;
    strcpy(config->home_ssids.values[0], "Home-WiFi");
    strcpy(config->home_ssids.values[1], "家庭网络-5G");
}

static enum location parse(const char *output, const struct charged_config *config) {
    return wifi_parse_status(output, strlen(output), config);
}

static void test_wifi_parser(void) {
    struct charged_config config;

    set_wifi_config(&config);
    CHECK(parse("Wifi is enabled\nWifi is connected to \"办公室网络\"\n", &config) ==
          LOCATION_OFFICE);
    CHECK(parse("Wifi is connected to \"家庭网络-5G\"\n", &config) == LOCATION_HOME);
    CHECK(parse("Wifi is connected to Company-WiFi\r\n", &config) == LOCATION_OFFICE);
    CHECK(parse("Wifi is connected to \"Company-WiFi\"\n"
                "Wifi is connected to \"Home-WiFi\"\n", &config) == LOCATION_HOME);
    CHECK(parse("Wifi is connected to \"Home-WiFi\"\n"
                "Wifi is connected to \"Company-WiFi\"\n", &config) == LOCATION_HOME);

    CHECK(parse("Wifi is enabled\nWifi is not connected\n", &config) == LOCATION_UNKNOWN);
    CHECK(parse("Wifi is connected to \"\"\n", &config) == LOCATION_UNKNOWN);
    CHECK(parse("Wifi is connected to \"Home-WiFi\n", &config) == LOCATION_UNKNOWN);
    CHECK(parse("Wifi is connected to \"Unknown-WiFi\" trailing\n", &config) ==
          LOCATION_UNKNOWN);
    CHECK(parse("prefix Wifi is connected to \"Home-WiFi\"\n", &config) ==
          LOCATION_UNKNOWN);
    CHECK(parse("WifiInfo: SSID: \"Home-WiFi\"\n", &config) == LOCATION_UNKNOWN);
    CHECK(parse("Wifi is connected to \"Cafe\\\"Guest\"\n", &config) == LOCATION_UNKNOWN);
}

int main(void) {
    test_policy();
    test_config();
    test_wifi_parser();
    printf("ok: %u checks\n", checks);
    return 0;
}
