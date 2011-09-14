#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/netlink.h>

#define DEBUG(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format, __func__, __LINE__, ## arg)

#define NOTE(format, arg...)            \
    fprintf(stderr, "udev.c - %s():%d - " format, __func__, __LINE__, ## arg)

int udev_main(void) {
	int sock;
	struct sockaddr_nl snl;
	const int on = 1;
	int pid;

	NOTE("Beginning udev support\n");

	sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
	if (sock == -1) {
		perror("Unable to get socket");
		return -1;
	}
	fcntl(sock, F_SETFD, FD_CLOEXEC);

	snl.nl_family = AF_NETLINK;
	snl.nl_groups = 1;

        if (bind(sock, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl))<0){
		perror("Unable to bind");
		return -2;
	}

	setsockopt(sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on));

	if ((pid=fork()))
		return pid;

	while(1) {
		char data[4096];
		int bytes;
		int byte;
		bytes = recv(sock, data, sizeof(data), 0);
		if (bytes < 0) {
			perror("Unable to receive data");
			return -3;
		}
		if (bytes == 0) {
			fprintf(stderr, "Kernel netlink closed\n");
			return -4;
		}
		DEBUG("Received %d bytes of data:\n", bytes);
		DEBUG("    ");
		for(byte=0; byte<bytes; byte++)
			fprintf(stderr, "%c", data[byte]?data[byte]:'.');
		fprintf(stderr, "\n");
	}


	return 0;
}
