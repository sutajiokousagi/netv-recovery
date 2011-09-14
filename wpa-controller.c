#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define CONFIG_FILE "/wpa.conf"


struct wpa_process {
    int pid;
    int pipe;
    char *ssid;
    char *key;
};

static int startswith(char *s1, char *s2) {
    return !strncmp(s1, s2, strlen(s2));
}

static void handle_chld(int sig) {
    fprintf(stderr, "SIGCHLD.  Waiting...\n");
    fprintf(stderr, "Wait result: %d\n", waitpid(-1, NULL, WNOHANG));
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
    fd = process->pipe;

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

        /* Indicates an error was encountered */
        if (bytes < 0) {
            if ((errno == EAGAIN) || (errno == EINTR))
                return 0;
            perror("Unable to read");
            return -1;
        }

        int i;
        for(i=0; line[i] && i<bytes; i++)
            if (line[i] == '\n' || line[i] == '\r')
                line[i] = '\0';
        fprintf(stderr, "Read %d bytes from %d.  Line: [%s]\n", bytes, fd, line);

        if (startswith(line, "Associated with "))
            return 1;

        if (startswith(line, "No network configuration "))
            return -1;

        return 0;
    }
    else if (ret == 0) {
        return 0;
    }
    else {
        perror("Unable to select");
        return -1;
    }
}

/* Starts wpa_supplicant.  For OPEN network, pass a NULL key. */
struct wpa_process *start_wpa(char *ssid, char *key, char *iface) {
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

    int p[2];
    pipe(p);
    signal(SIGCHLD, handle_chld);
    process->pid = fork();
    if (!process->pid) {
        close(p[0]);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[1]);

        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stdin, NULL, _IOLBF, 0);

        printf("Hi there!\n");
        execlp("wpa_supplicant", "-Dwext", "-i", iface, "-c" CONFIG_FILE, NULL);
        perror("Unable to exec");
        exit(1);
    }
    else if(process->pid < 0) {
        perror("Unable to fork");
        goto err;
    }

    close(p[1]);
    process->pipe = p[0];

    return process;

err:
    if (process && process->key)
        free(process->key);
    if (process && process->ssid)
        free(process->ssid);
    if (process && process->pipe)
        close(process->pipe);
    if (process && process->pid)
        kill(SIGTERM, process->pid);
    if(process)
        free(process);
    return NULL;
}

int stop_wpa(struct wpa_process *process) {
    if (process && process->key)
        free(process->key);
    if (process && process->ssid)
        free(process->ssid);
    if (process && process->pipe)
        close(process->pipe);
    if (process && process->pid)
        kill(SIGTERM, process->pid);
    free(process);
    return 0;
}
