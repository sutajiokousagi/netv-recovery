#ifdef linux
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef DBG
#define DEBUG_ADD(format, arg...)            \
    fprintf(stderr, format, __func__, __LINE__, ## arg)
#define DEBUG(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format, __func__, __LINE__, ## arg)
#else
#define DEBUG_ADD(format, arg...)
#define DEBUG(format, arg...)
#endif

#define ERROR(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format, __func__, __LINE__, ## arg)
#define NOTE(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format, __func__, __LINE__, ## arg)
#define PERROR(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format ": %s\n", __func__, __LINE__, ## arg, strerror(errno))

#define COMPAT_FIRMWARE_STR "SUBSYSTEM=compat_firmware"
#define FIRMWARE_STR "SUBSYSTEM=firmware"

#ifdef DBG
static void print_rule(char *data, int bytes) {
	int new_line = 1;
	DEBUG("Received %d bytes of data:\n", bytes);
	for(byte=0; byte<bytes; byte++) {
		if (new_line) {
			DEBUG("    ");
			new_line = 0;
		}
		if (data[byte])
			DEBUG_ADD("%c", data[byte]);
		else {
			DEBUG_ADD("\n");
			new_line = 1;
		}
	}
}
#endif

#define beginswith(x, y) (!strncmp(x, y, strlen(y)))

/*      add@/devices/platform/pxau2h-ehci/usb1/1-1/compat_firmware/1-1.ACTION=add.DEVPATH=/devices/platform/pxau2h-ehci/usb1/1-1/compat_firmware/1-1.SUBSYSTEM=compat_firmware.FIRMWARE=htc_9271.fw.TIMEOUT=60.ASYNC=0.SEQNUM=148.
*/
static int is_firmware_rule(char *msg, int len) {
	int i = 0;
	while (i < len) {
		if (beginswith(msg+i, COMPAT_FIRMWARE_STR)
		 || beginswith(msg+i, FIRMWARE_STR))
			return 1;
		/* The string is NULL-delimited.  Move to the next field. */
		while (i < len && msg[i])
			i++;
		i++;
	}
	return 0;
}

int load_firmware(char *msg, int len) {
	int i = 0;
	char *devpath = NULL;
	char *firmware = NULL;
	int fd, fw;
	char str[1024];

	while (i < len) {
		if (beginswith(msg+i, "FIRMWARE="))
			firmware = msg+i+strlen("FIRMWARE=");
		if (beginswith(msg+i, "DEVPATH="))
			devpath = msg+i+strlen("DEVPATH=");
		/* The string is NULL-delimited.  Move to the next field. */
		while (i < len && msg[i])
			i++;
		i++;
	}

	if (!firmware) {
		ERROR("Firmware string was not located!\n");
		return -1;
	}
	if (!devpath) {
		ERROR("Devpath string was not located!\n");
		return -2;
	}

	NOTE("Loading firmware %s to device at path %s\n", firmware, devpath);

	/* Open up the firmware blob */
	snprintf(str, sizeof(str), "/firmware/%s", firmware);
	fw = open(str, O_RDONLY);
	if (-1 == fw) {
		PERROR("Unable to open firmware file");
		return -3;
	}

	/* Indicate to the kernel that we're going to start writing data */
	snprintf(str, sizeof(str), "/sys%s/loading", devpath);
	fd = open(str, O_WRONLY);
	if (-1 == fd) {
		PERROR("Unable to open loading file");
		return -4;
	}
	write(fd, "1\n", 2);
	close(fd);

	/* Open up the pipe to the kernel */
	snprintf(str, sizeof(str), "/sys%s/data", devpath);
	fd = open(str, O_WRONLY);
	if (-1 == fd) {
		PERROR("Unable to open firmware output pipe");
		close(fw);
		return -5;
	}

	/* Begin writing data */
	while ((len = read(fw, str, sizeof(str))) > 0)
		write(fd, str, len);

	if (len < 0) {
		PERROR("Error while reading firmware file");
		close(fd);
		close(fw);
		return -6;
	}

	close(fd);
	close(fw);


	/* Indicate we're done */
	snprintf(str, sizeof(str), "/sys%s/loading", devpath);
	fd = open(str, O_WRONLY);
	if (-1 == fd) {
		PERROR("Unable to open loading file");
		return -4;
	}
	write(fd, "0\n", 2);
	close(fd);


	return 0;
}

int udev_main(void) {
	int sock;
	struct sockaddr_nl snl;
	const int on = 1;
	int pid;

	NOTE("Beginning udev support\n");

	sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sock == -1) {
		PERROR("Unable to get socket");
		return -1;
	}
	fcntl(sock, F_SETFD, FD_CLOEXEC);

	snl.nl_family = AF_NETLINK;
	snl.nl_groups = 1;

        if (bind(sock, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl))<0){
		PERROR("Unable to bind");
		return -2;
	}

	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));

	mkdir("/sys", 0777);
	if (-1 == mount("none", "/sys", "sysfs", 0, NULL)) {
		PERROR("Unable to mount sysfs");
		return -3;
	}

	mkdir("/proc", 0777);
	if (-1 == mount("none", "/proc", "procfs", 0, NULL)) {
		PERROR("Unable to mount procfs");
		return -3;
	}

	if ((pid=fork()))
		return pid;


	while(1) {
		char data[4096];
		int bytes;
		bytes = recv(sock, data, sizeof(data), 0);
		if (bytes < 0) {
			PERROR("Unable to receive data");
			exit(1);
		}
		if (bytes == 0) {
			ERROR("Kernel netlink closed\n");
			exit(2);
		}

#ifdef DBG
		print_rule(data, bytes);
#endif

		if (is_firmware_rule(data, bytes))
			load_firmware(data, bytes);
	}

	exit(3);
}
#else

int udev_main(void) {
    return 0;
}
#endif /* linux */
