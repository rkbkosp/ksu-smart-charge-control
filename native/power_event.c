#include "power_event.h"

#include <errno.h>
#include <linux/netlink.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define UEVENT_MAX_BYTES 8192

int power_event_open(void) {
    struct sockaddr_nl address;
    int receive_buffer = 64 * 1024;
    int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                    NETLINK_KOBJECT_UEVENT);

    if (fd < 0) {
        return -1;
    }
    memset(&address, 0, sizeof(address));
    address.nl_family = AF_NETLINK;
    address.nl_pid = (uint32_t)getpid();
    address.nl_groups = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

bool power_event_is_power_supply(const char *message, size_t length) {
    size_t offset = 0;
    static const char subsystem[] = "SUBSYSTEM=power_supply";

    while (offset < length) {
        size_t remaining = length - offset;
        size_t field_length = strnlen(message + offset, remaining);

        if (field_length == sizeof(subsystem) - 1 &&
            memcmp(message + offset, subsystem, sizeof(subsystem) - 1) == 0) {
            return true;
        }
        if (field_length == remaining) {
            break;
        }
        offset += field_length + 1;
    }
    return false;
}

int power_event_drain(int fd, bool *saw_power_supply) {
    char message[UEVENT_MAX_BYTES];

    *saw_power_supply = false;
    for (;;) {
        ssize_t length = recv(fd, message, sizeof(message), MSG_DONTWAIT);

        if (length < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        if (length == 0) {
            return 0;
        }
        if (power_event_is_power_supply(message, (size_t)length)) {
            *saw_power_supply = true;
        }
    }
}
