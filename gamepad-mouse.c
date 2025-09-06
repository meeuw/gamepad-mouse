/**
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If not,
 * see <https://www.gnu.org/licenses/>.
 */
 
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/input.h>
#include <libudev.h>
#include <time.h>
#include <linux/uinput.h>
#include <math.h>

typedef struct {
    uint32_t bits[32];  // 32 * 32 = 1024 bits
} bitset1024_t;

#define BITSET_INDEX(bit)     ((bit) / 32)
#define BITSET_OFFSET(bit)    ((bit) % 32)

#define BITSET_SET(arr, bit)   ((arr).bits[BITSET_INDEX(bit)] |=  (1U << BITSET_OFFSET(bit)))
#define BITSET_CLEAR(arr, bit) ((arr).bits[BITSET_INDEX(bit)] &= ~(1U << BITSET_OFFSET(bit)))
#define BITSET_TEST(arr, bit)  (((arr).bits[BITSET_INDEX(bit)] >> BITSET_OFFSET(bit)) & 1U)

int pipe_to_handler[2];
int pipe_from_handler[2];

ssize_t emit(const int fd, const int type, const int code, const int value) {
    const struct input_event ie = { { 0, 0 }, type, code, value };
    return write(fd, &ie, sizeof(ie));
}

typedef enum {
    ENUM_REL_X,
    ENUM_REL_Y,
    ENUM_REL_HWHEEL_HI_RES,
    ENUM_REL_WHEEL_HI_RES,
    ENUM_REL_TOTAL
} rel_enum;

struct mouse_data {
    int max;
    int offset;
    int value;
};

struct mouse_data mouse_data[ENUM_REL_TOTAL];

void emit_abs2rel(const int fd) {
    int rel[] = { REL_X, REL_Y, REL_HWHEEL_HI_RES, REL_WHEEL_HI_RES };
    static double values[] = {0, 0, 0, 0};
    bool do_syn = false;
    for (int i = ENUM_REL_X; i < ENUM_REL_TOTAL; i++) {
        const struct mouse_data mouse_data_v = mouse_data[i];
        values[i] += tan((double)(mouse_data_v.value - mouse_data_v.offset) * (M_PI / 2) / mouse_data_v.max);
        const int int_value = (int)values[i];
        values[i] -= int_value;
        if (int_value != 0) {
            do_syn = true;
            emit(fd, EV_REL, rel[i], int_value);
        }
    }
    if (do_syn) emit(fd, EV_SYN, SYN_REPORT, 0);
}

void write_to_handler(const char* buf, const size_t tel) {
    if (pipe_to_handler[1]) {
        write(pipe_to_handler[1], buf, tel);
    }
}

void handle_event(const struct input_event *ev, const int uinput_fd) {
    static bitset1024_t pressed_keys = {0};
    static bool mouse_mode = false;
    switch (ev->type) {
        case EV_SYN:
            break;
        case EV_ABS:
            if (!mouse_mode) {
                write_to_handler("mouse\n", 6);
                mouse_mode = true;
            }

            int value = ev->value;
            if (value > -2 && value < 0) value = 0;

            switch (ev->code) {
                case ABS_X:
                    mouse_data[ENUM_REL_X].value = value;
                    break;
                case ABS_Y:
                    mouse_data[ENUM_REL_Y].value = value;
                    break;
                case ABS_RX:
                    mouse_data[ENUM_REL_HWHEEL_HI_RES].value = value;
                    break;
                case ABS_RY:
                    mouse_data[ENUM_REL_WHEEL_HI_RES].value = value;
                    break;
                case ABS_HAT0X:
                    if (ev->value == 0) {
                        if (BITSET_TEST(pressed_keys, BTN_BACK)) {
                            BITSET_CLEAR(pressed_keys, BTN_BACK);
                            emit(uinput_fd, EV_KEY, BTN_BACK, 0);
                        }
                        if (BITSET_TEST(pressed_keys, BTN_FORWARD)) {
                            BITSET_CLEAR(pressed_keys, BTN_FORWARD);
                            emit(uinput_fd, EV_KEY, BTN_FORWARD, 0);
                        }
                    } else { 
                        const int pressed_key = ev->value < 0 ? BTN_BACK : BTN_FORWARD;
                        BITSET_SET(pressed_keys, pressed_key);
                        emit(uinput_fd, EV_KEY, pressed_key, 1);
                    }
                    emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
                    break;
                default:
                    printf("unhandled type: %i code: %i value: %i\n", ev->type, ev->code, ev->value);
            }
            break;
        case EV_KEY:
            switch (ev->code) {
                case BTN_START:
                    if (ev->value == 1) {
                        mouse_mode = false;
                        write_to_handler("keyboard\n", 9);
                    }
                    break;
                case BTN_SOUTH:
                    emit(uinput_fd, EV_KEY, KEY_CAPSLOCK, ev->value);
                    break;
                case BTN_TL:
                    emit(uinput_fd, EV_KEY, KEY_LEFTCTRL, ev->value);
                    break;
                case BTN_TR:
                    emit(uinput_fd, EV_KEY, KEY_LEFTALT, ev->value);
                    break;
                case BTN_EAST:
                    emit(uinput_fd, EV_KEY, BTN_LEFT, ev->value);
                    break;
                case BTN_NORTH:
                    emit(uinput_fd, EV_KEY, BTN_MIDDLE, ev->value);
                    break;
                case BTN_WEST:
                    emit(uinput_fd, EV_KEY, BTN_RIGHT, ev->value);
                    break;
                default:
                    printf("unhandled type: %i code: %i value: %i\n", ev->type, ev->code, ev->value);
            }
            emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
            break;
        default:
            printf("unhandled type: %i code: %i value: %i\n", ev->type, ev->code, ev->value);
            break;
    }
}

int main(const int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /sys/...\n", argv[0]);
        fprintf(stderr, "Obtain the path using: udevadm info --export-db --property-match=ID_VENDOR_ID=31e3 --property-match=ID_INPUT_JOYSTICK=1\n");
        return EXIT_FAILURE;
    }

    if (argc == 3) {
        if (pipe(pipe_to_handler) == -1 || pipe(pipe_from_handler) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        const pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            dup2(pipe_to_handler[0], STDIN_FILENO);
            close(pipe_to_handler[0]);
            close(pipe_to_handler[1]);

            dup2(pipe_from_handler[1], STDOUT_FILENO);
            close(pipe_from_handler[1]);
            close(pipe_from_handler[0]);

            execlp(argv[2], argv[2], argv[1], NULL);
            perror("execlp failed");
            exit(EXIT_FAILURE);
        }

        close(pipe_to_handler[0]);
        close(pipe_from_handler[1]);
    }

    const int uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_CAPSLOCK);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFTCTRL);
    ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFTALT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_BACK);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_FORWARD);

    ioctl(uinput_fd, UI_SET_EVBIT, EV_REL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_X);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_Y);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_HWHEEL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(uinput_fd, UI_SET_RELBIT, REL_WHEEL_HI_RES);

    struct uinput_setup usetup = {
        .id = {
            .bustype = BUS_USB,
            .vendor = 0,
            .product = 0,
            .version = 0
        },
        .name = "Gamepad Mouse",
        .ff_effects_max = 0
    };

    ioctl(uinput_fd, UI_DEV_SETUP, &usetup);
    ioctl(uinput_fd, UI_DEV_CREATE);


    struct udev *udev = udev_new();
    if (!udev) {
        fprintf(stderr, "Failed to create udev context\n");
        return EXIT_FAILURE;
    }
    struct udev_device *dev = udev_device_new_from_syspath(udev, argv[1]);
    const char *devnode = udev_device_get_devnode(dev);

    const int fd = open(devnode, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Could not find/open input device by name");
        return EXIT_FAILURE;
    }


    for (int i = ENUM_REL_X; i < ENUM_REL_TOTAL; i++) {
        struct input_absinfo absinfo;
        switch (i) {
            case ENUM_REL_X:
                ioctl(fd, EVIOCGABS(ABS_X), &absinfo);
                break;
            case ENUM_REL_Y:
                ioctl(fd, EVIOCGABS(ABS_Y), &absinfo);
                break;
            case ENUM_REL_HWHEEL_HI_RES:
                ioctl(fd, EVIOCGABS(ABS_RX), &absinfo);
                break;
            case ENUM_REL_WHEEL_HI_RES:
                ioctl(fd, EVIOCGABS(ABS_RY), &absinfo);
                break;
        }
        mouse_data[i].max = (int)((abs(absinfo.maximum) + abs(absinfo.minimum)) / 1.92);
        mouse_data[i].offset = absinfo.value;
        mouse_data[i].value = absinfo.value;
    }

    if (ioctl(fd, EVIOCGRAB, (void*)1) < 0) {
        perror("EVIOCGRAB");
        close(fd);
        return EXIT_FAILURE;
    }
    long int last = 0;

    constexpr int interval = 5 * 1000;
    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = interval,
        };

        const int ready = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (ready < 0) {
            perror("select");
            break;
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const long int now = (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;

        if (now - last > interval) {
            emit_abs2rel(uinput_fd);
            last = now;
        }

        if (FD_ISSET(fd, &rfds)) {
            for (;;) {
                struct input_event evbuf[64];
                const ssize_t n = read(fd, evbuf, sizeof(evbuf));
                if (n < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (errno == ENODEV) {
                        fprintf(stderr, "read: No such device\n");
                        return 1;
                    }
                    perror("read");
                    break;
                } else if (n == 0) {
                    fprintf(stderr, "Device disconnected (EOF)\n");
                    break;
                } else if (n % sizeof(struct input_event) != 0) {
                    fprintf(stderr, "Short read: %zd bytes (not a multiple of input_event)\n", n);
                    break;
                } else {
                    const size_t cnt = n / sizeof(struct input_event);
                    for (size_t i = 0; i < cnt; ++i) {
                        handle_event(&evbuf[i], uinput_fd);
                    }
                }
            }
        }
    }
}
