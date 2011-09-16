#ifdef linux
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#if defined(__GLIBC__) && __GLIBC__ >=2 && __GLIBC_MINOR__ >= 1
#include <netpacket/packet.h>
#include <net/ethernet.h>
#else
#include <sys/types.h>
#include <netinet/if_ether.h>
#endif
#include <string.h>
#include <stdio.h>
#include <unistd.h>


int my_ifup(char *ifname) {
	int sockfd;
	struct ifreq ifr;

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (-1 == sockfd) {
		perror("Unable to create socket");
		return -1;
	}

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)-1);
	if (-1 == ioctl(sockfd, SIOCGIFFLAGS, &ifr)) {
		perror("Unable to make request for network flags");
		close(sockfd);
		return -2;
	}
	ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
	if (-1 == ioctl(sockfd, SIOCSIFFLAGS, &ifr)) {
		perror("Unable to make request to set network flags");
		close(sockfd);
		return -3;
	}

	close(sockfd);

	return 0;
}

#else

int my_ifup(char *ifname) {
    return 0;
}
#endif /* linux */
