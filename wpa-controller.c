#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/select.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_FILE "/wpa.conf"
#define WPA_COMMAND "wpa_supplicant -iwlan0 -c" CONFIG_FILE


struct wpa_process {
    FILE *prog;
    char *ssid;
    char *key;
};

static int startswith(char *s1, char *s2) {
    return !strncmp(s1, s2, strlen(s2));
}

static int create_config(struct wpa_process *process) {
    FILE *cfg;

    cfg = fopen(CONFIG_FILE, "w");
    if (!cfg)
        return -1;

    fprintf(cfg, "ap_scan=1\n");
    fprintf(cfg, "network={\n");
    fprintf(cfg, "\tssid=\"%s\"\n", process->ssid);
    fprintf(cfg, "\tscan_ssid=1\n");
    if (process->key)
        fprintf(cfg, "\tpsk=\"%s\"\n", process->key);
    else
        fprintf(cfg, "\tkey_mgmt=NONE\n");
    fprintf(cfg, "}\n");

    fclose(cfg);
    return 0;
}

int poll_wpa(struct wpa_process *process, int blocking) {
    struct timeval timeout;
    int ret;
    int fd;
    fd_set set;

    bzero(&timeout, sizeof(timeout));
    fd = fileno(process->prog);

    FD_ZERO(&set);
    FD_SET(fd, &set);

    if (blocking)
        ret = 1;
    else
        ret = select(fd+1, &set, NULL, NULL, &timeout);

    if (ret > 0) {
        char line[4096];
        int bytes;
        bzero(line, sizeof(line));
        bytes = read(fd, line, sizeof(line));

        /* This happens if wpa_supplicant quits */
        if (!bytes)
            return -1;

        fprintf(stderr, "Read %d bytes from %d.  Line: %s", bytes, fd, line);

        /* Indicates an error was encountered */
        if (bytes < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                return 0;
            return -1;
        }

        if (startswith(line, "CTRL-EVENT-CONNECTED "))
            return 1;

        return 0;
    }
    else if (ret == 0) {
        return 0;
    }
    else {
        perror("Unable to read!");
        return -1;
    }
}

/* Starts wpa_supplicant.  For OPEN network, pass a NULL key. */
struct wpa_process *start_wpa(char *ssid, char *key) {
    struct wpa_process *process = malloc(sizeof(struct wpa_process));
    bzero(process, sizeof(*process));

    process->ssid = malloc(strlen(ssid)+1);
    strcpy(process->ssid, ssid);
    
    process->key = NULL;
    if (key) {
        process->key = malloc(strlen(key)+1);
        strcpy(process->key, key);
    }

    create_config(process);

    process->prog = popen(WPA_COMMAND, "r");
    if (!process->prog)
        goto err;

    return process;

err:
    if (process && process->key)
        free(process->key);
    if (process && process->ssid)
        free(process->ssid);
    if (process && process->prog)
        pclose(process->prog);
    if(process)
        free(process);
    return NULL;
}

int stop_wpa(struct wpa_process *process) {
    if (process && process->key)
        free(process->key);
    if (process && process->ssid)
        free(process->ssid);
    if (process && process->prog)
        pclose(process->prog);
    free(process);
    return 0;
}
