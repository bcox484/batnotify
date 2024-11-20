#include <dirent.h>
#include <fcntl.h>
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <unistd.h>

#define TIMEOUT 5
#define LONG_TIMEOUT 300

char *glob_bat_path = NULL;
NotifyNotification *glob_notification = NULL;
bool glob_active_notification = false;

void bat_notify_init(double percentage, NotifyUrgency urgency_level) {
    /* Send notification with battery level */
    char message[25];
    snprintf(message, sizeof(message) - 1, "%.1lf%s", percentage,
             "% BATTERY LEVEL");

    notify_init("batnotify");
    glob_notification = notify_notification_new(message, NULL, NULL);

    notify_notification_set_app_name(glob_notification, "batnotify");
    notify_notification_set_timeout(glob_notification, NOTIFY_EXPIRES_NEVER);
    notify_notification_set_urgency(glob_notification, urgency_level);
}

void bat_notify_update(double percentage, NotifyUrgency urgency_level) {
    if (glob_notification != NULL) {
        char message[25];
        snprintf(message, sizeof(message) - 1, "%.1lf%s", percentage,
                 "% BATTERY LEVEL");
        notify_notification_update(glob_notification, message, NULL, NULL);
    } else {
        bat_notify_init(percentage, urgency_level);
    }
    glob_active_notification = true;
    notify_notification_show(glob_notification, NULL);
}

void get_energy_level(const char *filename, double *value_store) {
    /* Function used to retrieve energy_now and energy_full */
    int fd;
    struct stat file_stat;
    size_t fsize;
    char *buf;

    if ((fd = open(filename, O_RDONLY)) == -1) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    if (fstat(fd, &file_stat) == -1) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }

    fsize = file_stat.st_size + 1;
    buf = calloc(1, fsize);
    if (buf == NULL) {
        close(fd);
        free(glob_bat_path);
        perror("get_energy_level: unable to allocate memory");
        exit(EXIT_FAILURE);
    }

    read(fd, buf, fsize - 1);
    close(fd);

    buf[fsize - 1] = '\0';
    *value_store = strtod(buf, NULL);
    free(buf);
}

void free_bat_path_on_kill(int signum) {
    free(glob_bat_path);

    if (glob_notification != NULL) {
        notify_notification_close(glob_notification, NULL);
        g_object_unref(G_OBJECT(glob_notification));
        notify_uninit();
    }

    exit(EXIT_SUCCESS);
}

void battery_path() {
    /* Find the battery directoy path, store in glob_bat_path */
    const char power_path[] = "/sys/class/power_supply/";
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(power_path)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strstr(ent->d_name, "BAT")) {
                size_t size = sizeof(power_path) + strlen(ent->d_name) + 2;
                glob_bat_path = calloc(1, size);
                snprintf(glob_bat_path, size - 1, "%s%s", power_path,
                         ent->d_name);
                break;
            }
        }
        closedir(dir);
    }
}

int status_label(size_t bat_path_size) {
    /* Determine if battery is discharging by returning output of strcmp, */
    /* if battery is dicharging will return EXIT_SUCCESS */

    size_t status_str_size = bat_path_size + 9;
    char *status_path = calloc(1, status_str_size);
    int fd;

    if (status_path == NULL) {
        free(glob_bat_path);
        perror("status_label: unable to allocate memory");
        exit(EXIT_FAILURE);
    }
    snprintf(status_path, status_str_size - 1, "%s%s", glob_bat_path,
             "/status");
    if ((fd = open(status_path, O_RDONLY)) == -1) {
        fprintf(stderr, "Unable to open file %s\n", status_path);
        free(status_path);
        free(glob_bat_path);
        exit(EXIT_FAILURE);
    }
    free(status_path);

    struct stat fl;
    if (fstat(fd, &fl) == -1) {
        perror("fstat status");
        free(glob_bat_path);
        exit(EXIT_FAILURE);
    }

    size_t len = fl.st_size + 1;
    char *status = calloc(1, len);

    if (status == NULL) {
        free(glob_bat_path);
        close(fd);
        perror("status_label: unable to allocate memory");
        exit(EXIT_FAILURE);
    }

    read(fd, status, len);
    close(fd);

    status[len - 1] = '\0';

    int state = strcmp("Discharging\n", status);
    free(status);
    return state;
}

void main_loop(double bat_percent_trigger, NotifyUrgency urgency_level) {
    /* Checks battery level and state every 15 seconds to determine if */
    /* notification should be sent */

    size_t bat_path_size = strlen(glob_bat_path);
    while (1) {
        int state = status_label(bat_path_size);
        double full = 0.0, current = 0.0;

        if (state == 0) {
            size_t full_str_size = bat_path_size + 14;
            size_t now_str_size = bat_path_size + 13;
            char *full_path = calloc(1, full_str_size);
            char *now_path = calloc(1, now_str_size);

            if (full_path == NULL) {
                free(glob_bat_path);
                perror("main_loop: unable to allocate memory");
                exit(EXIT_FAILURE);

            } else if (now_path == NULL) {
                free(glob_bat_path);
                perror("main_loop: unable to allocate memory");
                exit(EXIT_FAILURE);
            }

            snprintf(full_path, full_str_size - 1, "%s%s", glob_bat_path,
                     "/energy_full");
            snprintf(now_path, now_str_size - 1, "%s%s", glob_bat_path,
                     "/energy_now");
            get_energy_level(full_path, &full);
            get_energy_level(now_path, &current);

            free(full_path);
            free(now_path);
        }

        double percentage = 0.0;
        if (current > 0.0 && full > 0.0) {
            percentage = ((current / full) * 100.0);
        }

        if (state == 0 && percentage <= bat_percent_trigger &&
            percentage > 0.0) {
            bat_notify_update(percentage, urgency_level);
        } else if (glob_active_notification) {
            if (glob_notification != NULL) {
                notify_notification_close(glob_notification, NULL);
                glob_active_notification = false;
            }
        }

        if (percentage >= bat_percent_trigger + 10.0) {
            sleep(LONG_TIMEOUT);
        } else {
            sleep(TIMEOUT);
        }
    }
}

int main(int argc, char **argv) {
    const char help_message[] = {
        "Usage: batnotify [OPTIONS]\n"
        "Trigger a notification when battery drops below a certain "
        "level\n\n"
        "Must use at least one option to successfully use batnotify\n"
        "OPTIONS:\n"
        "  -p            Battery percentage (float) that triggers "
        "notification.\n"
        "  -u            Urgency of notification to be triggered low, normal, "
        "\n                critical. Defaults to normal\n\n"
        "  -d            Default values set percentage to 30.0, urgency to "
        "normal\n"
        "  -h, --help    Show help\n"
        "EXAMPLE:\n"
        "  batnotify -p 20.0 -u critical\n"
        "  batnotify -p 30.0 -u normal\n"
        "  batnotify -d\n"};

    if (argc < 2) {
        printf("%s\n", help_message);
        return EXIT_SUCCESS;
    }
    double bat_percent_trigger = 0.0;

    NotifyUrgency urgency_level = NOTIFY_URGENCY_NORMAL;
    bool one_option = false;

    for (int i = 1; i < argc; i++) {
        char *val = argv[i + 1];
        switch (argv[i][1]) {
        case 'd':
            bat_percent_trigger = 30.0;
            continue;

        case 'p':
            if (val == NULL) {
                break;
            }
            bat_percent_trigger = strtod(val, NULL);
            continue;

        case 'u':
            if (val == NULL) {
                break;
            }
            one_option = true;
            if (strcmp(val, "critical") == 0) {
                urgency_level = NOTIFY_URGENCY_CRITICAL;

            } else if (strcmp(val, "low") == 0) {
                urgency_level = NOTIFY_URGENCY_LOW;

            } else if (strcmp(val, "normal") == 0) {
            } else {
                printf("%s\n", help_message);
                fprintf(stderr, "%s is an invalid input\n", val);
                free(glob_bat_path);
                return EXIT_FAILURE;
            }
            continue;

        case 'h':
            free(glob_bat_path);
            printf("%s\n", help_message);
            return EXIT_SUCCESS;
        }

        if (strcmp("--help", argv[i]) == 0) {
            free(glob_bat_path);
            printf("%s\n", help_message);
            return EXIT_SUCCESS;
        }
    }

    if (one_option && bat_percent_trigger == 0.0) {
        bat_percent_trigger = 30.0;
    }

    if (bat_percent_trigger <= 0 || bat_percent_trigger > 100.0) {
        printf("%s\n", help_message);
        fprintf(stderr, "%lf %s\n", bat_percent_trigger, "Is invalid input");
        free(glob_bat_path);
        return EXIT_FAILURE;
    }

    battery_path();
    if (glob_bat_path == NULL) {
        perror("Null ptr main");
        return EXIT_FAILURE;
    }

    struct sigaction sa;
    sa.sa_handler = free_bat_path_on_kill;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    main_loop(bat_percent_trigger, urgency_level);
    return EXIT_SUCCESS;
}
