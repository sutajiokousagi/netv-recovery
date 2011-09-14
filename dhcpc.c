/* vi: set sw=4 ts=4: */
/*
 * udhcp client
 *
 * Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <syslog.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <values.h>
#include <signal.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <bits/time.h>

#include <asm/types.h>
#if (defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 1) || defined(_NEWLIB_VERSION)
# include <netpacket/packet.h>
# include <net/ethernet.h>
#else
# include <linux/if_packet.h>
# include <linux/if_ether.h>
#endif
#include <linux/filter.h>
#include <linux/ip.h>
#include <linux/if.h>
#include <linux/udp.h>
#include <linux/sockios.h>

#ifndef offsetof
# define offsetof(T,F) ((unsigned int)((char *)&((T *)0L)->F - (char *)0L))
#endif

# define move_from_unaligned32(v, u32p) (memcpy(&(v), (u32p), 4))
# define move_to_unaligned32(u32p, v) do { \
        uint32_t __t = (v); \
        memcpy((u32p), &__t, 4); \
} while (0)


#define LOG1(format, arg...)

#define NOTE(format, arg...)            \
    fprintf(stderr, "dhcpc.c - %s():%d - " format, __func__, __LINE__, ## arg)

#define ERROR(format, arg...)            \
    fprintf(stderr, "dhcpc.c - %s():%d - " format, __func__, __LINE__, ## arg)


// Die if we can't allocate and zero size bytes of memory.
static void* xzalloc(size_t size)
{
        void *ptr = malloc(size);
        memset(ptr, 0, size);
        return ptr;
}

static char* strncpy_IFNAMSIZ(char *dst, const char *src)
{
#ifndef IFNAMSIZ
        enum { IFNAMSIZ = 16 };
#endif  
        return strncpy(dst, src, IFNAMSIZ);
}

static const int const_int_1 = 1;
static void setsockopt_reuseaddr(int fd)
{
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &const_int_1, sizeof(const_int_1));
}
static int setsockopt_broadcast(int fd)
{
        return setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &const_int_1, sizeof(const_int_1));
}

static int setsockopt_bindtodevice(int fd, const char *iface)
{
        int r;
        struct ifreq ifr;
        strncpy_IFNAMSIZ(ifr.ifr_name, iface);
        /* NB: passing (iface, strlen(iface) + 1) does not work!
         * (maybe it works on _some_ kernels, but not on 2.6.26)
         * Actually, ifr_name is at offset 0, and in practice
         * just giving char[IFNAMSIZ] instead of struct ifreq works too.
         * But just in case it's not true on some obscure arch... */
        r = setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr));
        if (r)  
                ERROR("can't bind to interface %s", iface);
        return r;
}

static void get_mono(struct timespec *ts)
{
        if (syscall(__NR_clock_gettime, CLOCK_MONOTONIC, ts))
                ERROR("clock_gettime(MONOTONIC) failed");
}
static unsigned long long monotonic_us(void)
{
        struct timespec ts;
        get_mono(&ts);
        return ts.tv_sec * 1000000ULL + ts.tv_nsec/1000;
}
static unsigned monotonic_sec(void)
{
        struct timespec ts;
        get_mono(&ts);
        return ts.tv_sec;
}


/* DHCP protocol. See RFC 2131 */
#define DHCP_MAGIC              0x63825363
#define DHCP_OPTIONS_BUFSIZE    308
#define BOOTREQUEST             1
#define BOOTREPLY               2
#define SERVER_PORT             67
#define CLIENT_PORT             68
#define DHCP_MAX_SIZE           0x39

//TODO: rename ciaddr/yiaddr/chaddr
struct dhcp_packet {
        uint8_t op;      /* BOOTREQUEST or BOOTREPLY */
        uint8_t htype;   /* hardware address type. 1 = 10mb ethernet */
        uint8_t hlen;    /* hardware address length */
        uint8_t hops;    /* used by relay agents only */
        uint32_t xid;    /* unique id */
        uint16_t secs;   /* elapsed since client began acquisition/renewal */
        uint16_t flags;  /* only one flag so far: */
#define BROADCAST_FLAG 0x8000 /* "I need broadcast replies" */
        uint32_t ciaddr; /* client IP (if client is in BOUND, RENEW or REBINDING state) */
        uint32_t yiaddr; /* 'your' (client) IP address */
        /* IP address of next server to use in bootstrap, returned in DHCPOFFER, DHCPACK by server */
        uint32_t siaddr_nip;
        uint32_t gateway_nip; /* relay agent IP address */
        uint8_t chaddr[16];   /* link-layer client hardware address (MAC) */
        uint8_t sname[64];    /* server host name (ASCIZ) */
        uint8_t file[128];    /* boot file name (ASCIZ) */
        uint32_t cookie;      /* fixed first four option bytes (99,130,83,99 dec) */
        uint8_t options[DHCP_OPTIONS_BUFSIZE + 80];
} __attribute__ ((packed));

struct client_config_t {
        uint8_t client_mac[6];          /* Our mac address */
        char no_default_options;        /* Do not include default options in request */
        int ifindex;                    /* Index number of the interface to use */
        uint8_t opt_mask[256 / 8];      /* Bitmask of options to send (-O option) */
        const char *interface;          /* The name of the interface to use */
        struct option_set *options;     /* list of DHCP options to send to server */
        uint8_t *clientid;              /* Optional client id to use */
        uint8_t *vendorclass;           /* Optional vendor class-id to use */
        uint8_t *hostname;              /* Optional hostname to use */
        uint8_t *fqdn;                  /* Optional fully qualified domain name to use */
};

struct dhcp_optflag {
        uint8_t flags;
        uint8_t code;
};

struct option_set {
        uint8_t *data;
        struct option_set *next;
};

typedef int smallint;
typedef unsigned smalluint;

#define DHCP_PKT_SNAME_LEN      64
#define DHCP_PKT_FILE_LEN      128
#define DHCP_PKT_SNAME_LEN_STR "64"
#define DHCP_PKT_FILE_LEN_STR "128"


/* DHCP option codes (partial list). See RFC 2132 and
 * http://www.iana.org/assignments/bootp-dhcp-parameters/
 * Commented out options are handled by common option machinery,
 * uncommented ones have spacial cases (grep for them to see).
 */
#define DHCP_PADDING            0x00
#define DHCP_SUBNET             0x01
//#define DHCP_TIME_OFFSET      0x02 /* (localtime - UTC_time) in seconds. signed */
//#define DHCP_ROUTER           0x03
//#define DHCP_TIME_SERVER      0x04 /* RFC 868 time server (32-bit, 0 = 1.1.1900) */
//#define DHCP_NAME_SERVER      0x05 /* IEN 116 _really_ ancient kind of NS */
//#define DHCP_DNS_SERVER       0x06
//#define DHCP_LOG_SERVER       0x07 /* port 704 UDP log (not syslog)
//#define DHCP_COOKIE_SERVER    0x08 /* "quote of the day" server */
//#define DHCP_LPR_SERVER       0x09
#define DHCP_HOST_NAME          0x0c /* either client informs server or server gives name to client */
//#define DHCP_BOOT_SIZE        0x0d
//#define DHCP_DOMAIN_NAME      0x0f /* server gives domain suffix */
//#define DHCP_SWAP_SERVER      0x10
//#define DHCP_ROOT_PATH        0x11
//#define DHCP_IP_TTL           0x17
//#define DHCP_MTU              0x1a
//#define DHCP_BROADCAST        0x1c
//#define DHCP_ROUTES           0x21
//#define DHCP_NIS_DOMAIN       0x28
//#define DHCP_NIS_SERVER       0x29
//#define DHCP_NTP_SERVER       0x2a
//#define DHCP_WINS_SERVER      0x2c
#define DHCP_REQUESTED_IP       0x32 /* sent by client if specific IP is wanted */
#define DHCP_LEASE_TIME         0x33
#define DHCP_OPTION_OVERLOAD    0x34
#define DHCP_MESSAGE_TYPE       0x35
#define DHCP_SERVER_ID          0x36 /* by default server's IP */
#define DHCP_PARAM_REQ          0x37 /* list of options client wants */
//#define DHCP_ERR_MESSAGE      0x38 /* error message when sending NAK etc */
#define DHCP_MAX_SIZE           0x39
#define DHCP_VENDOR             0x3c /* client's vendor (a string) */
#define DHCP_CLIENT_ID          0x3d /* by default client's MAC addr, but may be arbitrarily long */
//#define DHCP_TFTP_SERVER_NAME 0x42 /* same as 'sname' field */
//#define DHCP_BOOT_FILE        0x43 /* same as 'file' field */
//#define DHCP_USER_CLASS       0x4d /* RFC 3004. set of LASCII strings. "I am a printer" etc */
#define DHCP_FQDN               0x51 /* client asks to update DNS to map its FQDN to its new IP */
//#define DHCP_DOMAIN_SEARCH    0x77 /* RFC 3397. set of ASCIZ string, DNS-style compressed */
//#define DHCP_SIP_SERVERS      0x78 /* RFC 3361. flag byte, then: 0: domain names, 1: IP addrs */
//#define DHCP_STATIC_ROUTES    0x79 /* RFC 3442. (mask,ip,router) tuples */
//#define DHCP_MS_STATIC_ROUTES 0xf9 /* Microsoft's pre-RFC 3442 code for 0x79? */
//#define DHCP_WPAD             0xfc /* MSIE's Web Proxy Autodiscovery Protocol */
#define DHCP_END                0xff

/* Offsets in option byte sequence */
#define OPT_CODE                0
#define OPT_LEN                 1
#define OPT_DATA                2
/* Bits in "overload" option */
#define OPTION_FIELD            0
#define FILE_FIELD              1
#define SNAME_FIELD             2

/* DHCP_MESSAGE_TYPE values */
#define DHCPDISCOVER            1 /* client -> server */
#define DHCPOFFER               2 /* client <- server */
#define DHCPREQUEST             3 /* client -> server */
#define DHCPDECLINE             4 /* client -> server */
#define DHCPACK                 5 /* client <- server */
#define DHCPNAK                 6 /* client <- server */
#define DHCPRELEASE             7 /* client -> server */
#define DHCPINFORM              8 /* client -> server */
#define DHCP_MINTYPE DHCPDISCOVER
#define DHCP_MAXTYPE DHCPINFORM

struct ip_udp_dhcp_packet {
        struct iphdr ip;
        struct udphdr udp;
        struct dhcp_packet data;
} __attribute__ ((packed));

struct udp_dhcp_packet {
        struct udphdr udp;
        struct dhcp_packet data;
} __attribute__ ((packed));

enum {  
        IP_UDP_DHCP_SIZE = sizeof(struct ip_udp_dhcp_packet) - 80,
        UDP_DHCP_SIZE    = sizeof(struct udp_dhcp_packet) - 80,
        DHCP_SIZE        = sizeof(struct dhcp_packet) - 80,
};

enum {  
        OPTION_IP = 1,
        OPTION_IP_PAIR,
        OPTION_STRING,
//      OPTION_BOOLEAN,
        OPTION_U8,
        OPTION_U16,
//      OPTION_S16,
        OPTION_U32,
        OPTION_S32,
        OPTION_BIN,
        OPTION_STATIC_ROUTES,
#if ENABLE_FEATURE_UDHCP_RFC3397
        OPTION_DNS_STRING,  /* RFC1035 compressed domain name list */
        OPTION_SIP_SERVERS,
#endif  

        OPTION_TYPE_MASK = 0x0f,
        /* Client requests this option by default */
        OPTION_REQ  = 0x10,
        /* There can be a list of 1 or more of these */
        OPTION_LIST = 0x20,
};

const uint8_t MAC_BCAST_ADDR[6] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const struct dhcp_optflag dhcp_optflags[] = {
        /* flags                                    code */
        { OPTION_IP                   | OPTION_REQ, 0x01 }, /* DHCP_SUBNET        */
        { OPTION_S32                              , 0x02 }, /* DHCP_TIME_OFFSET   */
        { OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x03 }, /* DHCP_ROUTER        */
//      { OPTION_IP | OPTION_LIST                 , 0x04 }, /* DHCP_TIME_SERVER   */
//      { OPTION_IP | OPTION_LIST                 , 0x05 }, /* DHCP_NAME_SERVER   */
        { OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x06 }, /* DHCP_DNS_SERVER    */
//      { OPTION_IP | OPTION_LIST                 , 0x07 }, /* DHCP_LOG_SERVER    */
//      { OPTION_IP | OPTION_LIST                 , 0x08 }, /* DHCP_COOKIE_SERVER */
        { OPTION_IP | OPTION_LIST                 , 0x09 }, /* DHCP_LPR_SERVER    */
        { OPTION_STRING               | OPTION_REQ, 0x0c }, /* DHCP_HOST_NAME     */
        { OPTION_U16                              , 0x0d }, /* DHCP_BOOT_SIZE     */
        { OPTION_STRING               | OPTION_REQ, 0x0f }, /* DHCP_DOMAIN_NAME   */
        { OPTION_IP                               , 0x10 }, /* DHCP_SWAP_SERVER   */
        { OPTION_STRING                           , 0x11 }, /* DHCP_ROOT_PATH     */
        { OPTION_U8                               , 0x17 }, /* DHCP_IP_TTL        */
        { OPTION_U16                              , 0x1a }, /* DHCP_MTU           */
        { OPTION_IP                   | OPTION_REQ, 0x1c }, /* DHCP_BROADCAST     */
        { OPTION_IP_PAIR | OPTION_LIST            , 0x21 }, /* DHCP_ROUTES        */
        { OPTION_STRING                           , 0x28 }, /* DHCP_NIS_DOMAIN    */
        { OPTION_IP | OPTION_LIST                 , 0x29 }, /* DHCP_NIS_SERVER    */
        { OPTION_IP | OPTION_LIST     | OPTION_REQ, 0x2a }, /* DHCP_NTP_SERVER    */
        { OPTION_IP | OPTION_LIST                 , 0x2c }, /* DHCP_WINS_SERVER   */
        { OPTION_U32                              , 0x33 }, /* DHCP_LEASE_TIME    */
        { OPTION_IP                               , 0x36 }, /* DHCP_SERVER_ID     */
        { OPTION_STRING                           , 0x38 }, /* DHCP_ERR_MESSAGE   */
//TODO: must be combined with 'sname' and 'file' handling:
        { OPTION_STRING                           , 0x42 }, /* DHCP_TFTP_SERVER_NAME */
        { OPTION_STRING                           , 0x43 }, /* DHCP_BOOT_FILE     */
//TODO: not a string, but a set of LASCII strings:
//      { OPTION_STRING                           , 0x4D }, /* DHCP_USER_CLASS    */
#if ENABLE_FEATURE_UDHCP_RFC3397
        { OPTION_DNS_STRING | OPTION_LIST         , 0x77 }, /* DHCP_DOMAIN_SEARCH */
        { OPTION_SIP_SERVERS                      , 0x78 }, /* DHCP_SIP_SERVERS   */
#endif  
        { OPTION_STATIC_ROUTES                    , 0x79 }, /* DHCP_STATIC_ROUTES */
        { OPTION_STATIC_ROUTES                    , 0xf9 }, /* DHCP_MS_STATIC_ROUTES */
        { OPTION_STRING                           , 0xfc }, /* DHCP_WPAD          */

        /* Options below have no match in dhcp_option_strings[],
         * are not passed to dhcpc scripts, and cannot be specified
         * with "option XXX YYY" syntax in dhcpd config file.
         * These entries are only used internally by udhcp[cd]
         * to correctly encode options into packets.
         */

        { OPTION_IP                               , 0x32 }, /* DHCP_REQUESTED_IP  */
        { OPTION_U8                               , 0x35 }, /* DHCP_MESSAGE_TYPE  */
        { OPTION_U16                              , 0x39 }, /* DHCP_MAX_SIZE      */
//looks like these opts will work just fine even without these defs:
//      { OPTION_STRING                           , 0x3c }, /* DHCP_VENDOR        */
//      /* not really a string: */
//      { OPTION_STRING                           , 0x3d }, /* DHCP_CLIENT_ID     */
        { 0, 0 } /* zeroed terminating entry */
};


const uint8_t dhcp_option_lengths[] = {
        [OPTION_IP] =      4,
        [OPTION_IP_PAIR] = 8,
//      [OPTION_BOOLEAN] = 1,
        [OPTION_STRING] =  1,  /* ignored by udhcp_str2optset */
#if ENABLE_FEATURE_UDHCP_RFC3397
        [OPTION_DNS_STRING] = 1,  /* ignored by both udhcp_str2optset and xmalloc_optname_optval */
        [OPTION_SIP_SERVERS] = 1,
#endif  
        [OPTION_U8] =      1,
        [OPTION_U16] =     2,
//      [OPTION_S16] =     2,
        [OPTION_U32] =     4,
        [OPTION_S32] =     4,
        /* Just like OPTION_STRING, we use minimum length here */
        [OPTION_STATIC_ROUTES] = 5,
};


static void udhcp_init_header(struct dhcp_packet *packet, char type);
static int udhcp_send_raw_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port, const uint8_t *dest_arp,
		int ifindex);
static int udhcp_send_kernel_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port);
static uint16_t udhcp_checksum(void *addr, int count);
static int udhcp_listen_socket(/*uint32_t ip,*/ int port, const char *inf);


/*** Script execution code ***/



/* Call a script with a par file and env vars */
static void udhcp_run_script(struct dhcp_packet *packet, const char *name)
{
	NOTE("Need to run script %s\n", name);
}


/*** Sending/receiving packets ***/

static inline uint32_t random_xid(void)
{
	return rand();
}

/* Return the position of the 'end' option (no bounds checking) */
static int udhcp_end_option(uint8_t *optionptr)
{
        int i = 0;

        while (optionptr[i] != DHCP_END) {
                if (optionptr[i] != DHCP_PADDING)
                        i += optionptr[i + OPT_LEN] + OPT_DATA-1;
                i++;
        }
        return i;
}

/* Add an option (supplied in binary form) to the options.
 * Option format: [code][len][data1][data2]..[dataLEN]
 */
static void udhcp_add_binary_option(struct dhcp_packet *packet, uint8_t *addopt)
{
        unsigned len;
        uint8_t *optionptr = packet->options;
        unsigned end = udhcp_end_option(optionptr);

        len = OPT_DATA + addopt[OPT_LEN];
        /* end position + (option code/length + addopt length) + end option */
        if (end + len + 1 >= DHCP_OPTIONS_BUFSIZE) {
//TODO: learn how to use overflow option if we exhaust packet->options[]
                ERROR("option 0x%02x did not fit into the packet",
                                addopt[OPT_CODE]);
                return;
        }
        memcpy(optionptr + end, addopt, len);
        optionptr[end + len] = DHCP_END;
}

/* Add an one to four byte option to a packet */
static void udhcp_add_simple_option(struct dhcp_packet *packet, uint8_t code, uint32_t data)
{
        const struct dhcp_optflag *dh;

        for (dh = dhcp_optflags; dh->code; dh++) {
                if (dh->code == code) {
                        uint8_t option[6], len;

                        option[OPT_CODE] = code;
                        len = dhcp_option_lengths[dh->flags & OPTION_TYPE_MASK];
                        option[OPT_LEN] = len;
                        /* Assignment is unaligned! */
                        move_to_unaligned32(&option[OPT_DATA], data);
                        udhcp_add_binary_option(packet, option);
                        return;
                }
        }

        ERROR("can't add option 0x%02x", code);
}



/* Initialize the packet with the proper defaults */
static void init_packet(struct client_config_t *cfg, struct dhcp_packet *packet, char type)
{
	/* Fill in: op, htype, hlen, cookie fields; message type option: */
	udhcp_init_header(packet, type);

	packet->xid = random_xid();

	memcpy(packet->chaddr, cfg->client_mac, 6);
	if (cfg->clientid)
		udhcp_add_binary_option(packet, cfg->clientid);
}

static void add_client_options(struct client_config_t *cfg, struct dhcp_packet *packet)
{
	uint8_t c;
	int i, end, len;

	udhcp_add_simple_option(packet, DHCP_MAX_SIZE, htons(IP_UDP_DHCP_SIZE));

	/* Add a "param req" option with the list of options we'd like to have
	 * from stubborn DHCP servers. Pull the data from the struct in common.c.
	 * No bounds checking because it goes towards the head of the packet. */
	end = udhcp_end_option(packet->options);
	len = 0;
	for (i = 0; (c = dhcp_optflags[i].code) != 0; i++) {
		if ((   (dhcp_optflags[i].flags & OPTION_REQ)
		     && !cfg->no_default_options
		    )
		 || (cfg->opt_mask[c >> 3] & (1 << (c & 7)))
		) {
			packet->options[end + OPT_DATA + len] = c;
			len++;
		}
	}
	if (len) {
		packet->options[end + OPT_CODE] = DHCP_PARAM_REQ;
		packet->options[end + OPT_LEN] = len;
		packet->options[end + OPT_DATA + len] = DHCP_END;
	}

	if (cfg->vendorclass)
		udhcp_add_binary_option(packet, cfg->vendorclass);
	if (cfg->hostname)
		udhcp_add_binary_option(packet, cfg->hostname);
	if (cfg->fqdn)
		udhcp_add_binary_option(packet, cfg->fqdn);

	/* Add -x options if any */
	{
		struct option_set *curr = cfg->options;
		while (curr) {
			udhcp_add_binary_option(packet, curr->data);
			curr = curr->next;
		}
//		if (cfg->sname)
//			strncpy((char*)packet->sname, cfg->sname, sizeof(packet->sname) - 1);
//		if (cfg->boot_file)
//			strncpy((char*)packet->file, cfg->boot_file, sizeof(packet->file) - 1);
	}
}

/* RFC 2131
 * 4.4.4 Use of broadcast and unicast
 *
 * The DHCP client broadcasts DHCPDISCOVER, DHCPREQUEST and DHCPINFORM
 * messages, unless the client knows the address of a DHCP server.
 * The client unicasts DHCPRELEASE messages to the server. Because
 * the client is declining the use of the IP address supplied by the server,
 * the client broadcasts DHCPDECLINE messages.
 *
 * When the DHCP client knows the address of a DHCP server, in either
 * INIT or REBOOTING state, the client may use that address
 * in the DHCPDISCOVER or DHCPREQUEST rather than the IP broadcast address.
 * The client may also use unicast to send DHCPINFORM messages
 * to a known DHCP server. If the client receives no response to DHCP
 * messages sent to the IP address of a known DHCP server, the DHCP
 * client reverts to using the IP broadcast address.
 */

static int raw_bcast_from_client_config_ifindex(struct client_config_t *cfg, struct dhcp_packet *packet)
{
	return udhcp_send_raw_packet(packet,
		/*src*/ INADDR_ANY, CLIENT_PORT,
		/*dst*/ INADDR_BROADCAST, SERVER_PORT, MAC_BCAST_ADDR,
		cfg->ifindex);
}

/* Broadcast a DHCP discover packet to the network, with an optionally requested IP */
static int send_discover(struct client_config_t *cfg, uint32_t xid, uint32_t requested)
{
	struct dhcp_packet packet;

	/* Fill in: op, htype, hlen, cookie, chaddr fields,
	 * random xid field (we override it below),
	 * client-id option (unless -C), message type option:
	 */
	init_packet(cfg, &packet, DHCPDISCOVER);

	packet.xid = xid;
	if (requested)
		udhcp_add_simple_option(&packet, DHCP_REQUESTED_IP, requested);

	/* Add options: maxsize,
	 * optionally: hostname, fqdn, vendorclass,
	 * "param req" option according to -O, options specified with -x
	 */
	add_client_options(cfg, &packet);

	NOTE("Sending discover...");
	return raw_bcast_from_client_config_ifindex(cfg, &packet);
}

/* Broadcast a DHCP request message */
/* RFC 2131 3.1 paragraph 3:
 * "The client _broadcasts_ a DHCPREQUEST message..."
 */
static int send_select(struct client_config_t *cfg, uint32_t xid, uint32_t server, uint32_t requested)
{
	struct dhcp_packet packet;
	struct in_addr addr;

/*
 * RFC 2131 4.3.2 DHCPREQUEST message
 * ...
 * If the DHCPREQUEST message contains a 'server identifier'
 * option, the message is in response to a DHCPOFFER message.
 * Otherwise, the message is a request to verify or extend an
 * existing lease. If the client uses a 'client identifier'
 * in a DHCPREQUEST message, it MUST use that same 'client identifier'
 * in all subsequent messages. If the client included a list
 * of requested parameters in a DHCPDISCOVER message, it MUST
 * include that list in all subsequent messages.
 */
	/* Fill in: op, htype, hlen, cookie, chaddr fields,
	 * random xid field (we override it below),
	 * client-id option (unless -C), message type option:
	 */
	init_packet(cfg, &packet, DHCPREQUEST);

	packet.xid = xid;
	udhcp_add_simple_option(&packet, DHCP_REQUESTED_IP, requested);

	udhcp_add_simple_option(&packet, DHCP_SERVER_ID, server);

	/* Add options: maxsize,
	 * optionally: hostname, fqdn, vendorclass,
	 * "param req" option according to -O, and options specified with -x
	 */
	add_client_options(cfg, &packet);

	addr.s_addr = requested;
	NOTE("Sending select for %s...", inet_ntoa(addr));
	return raw_bcast_from_client_config_ifindex(cfg, &packet);
}

/* Unicast or broadcast a DHCP renew message */
static int send_renew(struct client_config_t *cfg, uint32_t xid, uint32_t server, uint32_t ciaddr)
{
	struct dhcp_packet packet;

/*
 * RFC 2131 4.3.2 DHCPREQUEST message
 * ...
 * DHCPREQUEST generated during RENEWING state:
 *
 * 'server identifier' MUST NOT be filled in, 'requested IP address'
 * option MUST NOT be filled in, 'ciaddr' MUST be filled in with
 * client's IP address. In this situation, the client is completely
 * configured, and is trying to extend its lease. This message will
 * be unicast, so no relay agents will be involved in its
 * transmission.  Because 'giaddr' is therefore not filled in, the
 * DHCP server will trust the value in 'ciaddr', and use it when
 * replying to the client.
 */
	/* Fill in: op, htype, hlen, cookie, chaddr fields,
	 * random xid field (we override it below),
	 * client-id option (unless -C), message type option:
	 */
	init_packet(cfg, &packet, DHCPREQUEST);

	packet.xid = xid;
	packet.ciaddr = ciaddr;

	/* Add options: maxsize,
	 * optionally: hostname, fqdn, vendorclass,
	 * "param req" option according to -O, and options specified with -x
	 */
	add_client_options(cfg, &packet);

	NOTE("Sending renew...");
	if (server)
		return udhcp_send_kernel_packet(&packet,
			ciaddr, CLIENT_PORT,
			server, SERVER_PORT);
	return raw_bcast_from_client_config_ifindex(cfg, &packet);
}

/* Unicast a DHCP release message */
static int send_release(struct client_config_t *cfg, uint32_t server, uint32_t ciaddr)
{
	struct dhcp_packet packet;

	/* Fill in: op, htype, hlen, cookie, chaddr, random xid fields,
	 * client-id option (unless -C), message type option:
	 */
	init_packet(cfg, &packet, DHCPRELEASE);

	/* DHCPRELEASE uses ciaddr, not "requested ip", to store IP being released */
	packet.ciaddr = ciaddr;

	udhcp_add_simple_option(&packet, DHCP_SERVER_ID, server);

	NOTE("Sending release...");
	return udhcp_send_kernel_packet(&packet, ciaddr, CLIENT_PORT, server, SERVER_PORT);
}

/* Returns -1 on errors that are fatal for the socket, -2 for those that aren't */
static int udhcp_recv_raw_packet(struct dhcp_packet *dhcp_pkt, int fd)
{
	int bytes;
	struct ip_udp_dhcp_packet packet;
	uint16_t check;

	memset(&packet, 0, sizeof(packet));
	bytes = read(fd, &packet, sizeof(packet));
	if (bytes < 0) {
		LOG1("Packet read error, ignoring");
		/* NB: possible down interface, etc. Caller should pause. */
		return bytes; /* returns -1 */
	}

	if (bytes < (int) (sizeof(packet.ip) + sizeof(packet.udp))) {
		LOG1("Packet is too short, ignoring");
		return -2;
	}

	if (bytes < ntohs(packet.ip.tot_len)) {
		/* packet is bigger than sizeof(packet), we did partial read */
		LOG1("Oversized packet, ignoring");
		return -2;
	}

	/* ignore any extra garbage bytes */
	bytes = ntohs(packet.ip.tot_len);

	/* make sure its the right packet for us, and that it passes sanity checks */
	if (packet.ip.protocol != IPPROTO_UDP || packet.ip.version != IPVERSION
	 || packet.ip.ihl != (sizeof(packet.ip) >> 2)
	 || packet.udp.dest != htons(CLIENT_PORT)
	/* || bytes > (int) sizeof(packet) - can't happen */
	 || ntohs(packet.udp.len) != (uint16_t)(bytes - sizeof(packet.ip))
	) {
		LOG1("Unrelated/bogus packet, ignoring");
		return -2;
	}

	/* verify IP checksum */
	check = packet.ip.check;
	packet.ip.check = 0;
	if (check != udhcp_checksum(&packet.ip, sizeof(packet.ip))) {
		LOG1("Bad IP header checksum, ignoring");
		return -2;
	}

	/* verify UDP checksum. IP header has to be modified for this */
	memset(&packet.ip, 0, offsetof(struct iphdr, protocol));
	/* ip.xx fields which are not memset: protocol, check, saddr, daddr */
	packet.ip.tot_len = packet.udp.len; /* yes, this is needed */
	check = packet.udp.check;
	packet.udp.check = 0;
	if (check && check != udhcp_checksum(&packet, bytes)) {
		LOG1("Packet with bad UDP checksum received, ignoring");
		return -2;
	}

	memcpy(dhcp_pkt, &packet.data, bytes - (sizeof(packet.ip) + sizeof(packet.udp)));

	if (dhcp_pkt->cookie != htonl(DHCP_MAGIC)) {
		NOTE("Packet with bad magic, ignoring");
		return -2;
	}
	LOG1("Got valid DHCP packet");
	return bytes - (sizeof(packet.ip) + sizeof(packet.udp));
}


/*** Main ***/

static int sockfd = -1;

#define LISTEN_NONE   0
#define LISTEN_KERNEL 1
#define LISTEN_RAW    2
static smallint listen_mode;

/* initial state: (re)start DHCP negotiation */
#define INIT_SELECTING  0
/* discover was sent, DHCPOFFER reply received */
#define REQUESTING      1
/* select/renew was sent, DHCPACK reply received */
#define BOUND           2
/* half of lease passed, want to renew it by sending unicast renew requests */
#define RENEWING        3
/* renew requests were not answered, lease is almost over, send broadcast renew */
#define REBINDING       4
/* manually requested renew (SIGUSR1) */
#define RENEW_REQUESTED 5
/* release, possibly manually requested (SIGUSR2) */
#define RELEASED        6
static smallint state;

static int udhcp_raw_socket(int ifindex)
{
	int fd;
	struct sockaddr_ll sock;

	/*
	 * Comment:
	 *
	 *	I've selected not to see LL header, so BPF doesn't see it, too.
	 *	The filter may also pass non-IP and non-ARP packets, but we do
	 *	a more complete check when receiving the message in userspace.
	 *
	 * and filter shamelessly stolen from:
	 *
	 *	http://www.flamewarmaster.de/software/dhcpclient/
	 *
	 * There are a few other interesting ideas on that page (look under
	 * "Motivation").  Use of netlink events is most interesting.  Think
	 * of various network servers listening for events and reconfiguring.
	 * That would obsolete sending HUP signals and/or make use of restarts.
	 *
	 * Copyright: 2006, 2007 Stefan Rompf <sux@loplof.de>.
	 * License: GPL v2.
	 *
	 * TODO: make conditional?
	 */
#define SERVER_AND_CLIENT_PORTS  ((67 << 16) + 68)
	static const struct sock_filter filter_instr[] = {
		/* check for udp */
		BPF_STMT(BPF_LD|BPF_B|BPF_ABS, 9),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, IPPROTO_UDP, 2, 0),     /* L5, L1, is UDP? */
		/* ugly check for arp on ethernet-like and IPv4 */
		BPF_STMT(BPF_LD|BPF_W|BPF_ABS, 2),                      /* L1: */
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 0x08000604, 3, 4),      /* L3, L4 */
		/* skip IP header */
		BPF_STMT(BPF_LDX|BPF_B|BPF_MSH, 0),                     /* L5: */
		/* check udp source and destination ports */
		BPF_STMT(BPF_LD|BPF_W|BPF_IND, 0),
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SERVER_AND_CLIENT_PORTS, 0, 1),	/* L3, L4 */
		/* returns */
		BPF_STMT(BPF_RET|BPF_K, 0x0fffffff ),                   /* L3: pass */
		BPF_STMT(BPF_RET|BPF_K, 0),                             /* L4: reject */
	};
	static const struct sock_fprog filter_prog = {
		.len = sizeof(filter_instr) / sizeof(filter_instr[0]),
		/* casting const away: */
		.filter = (struct sock_filter *) filter_instr,
	};

	LOG1("Opening raw socket on ifindex %d", ifindex); //log2?

	fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	LOG1("Got raw socket fd %d", fd); //log2?

	if (SERVER_PORT == 67 && CLIENT_PORT == 68) {
		/* Use only if standard ports are in use */
		/* Ignoring error (kernel may lack support for this) */
		if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter_prog,
				sizeof(filter_prog)) >= 0)
			LOG1("Attached filter to raw socket fd %d", fd); // log?
	}

	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_IP);
	sock.sll_ifindex = ifindex;
	bind(fd, (struct sockaddr *) &sock, sizeof(sock));
	LOG1("Created raw socket");

	return fd;
}

static void change_listen_mode(struct client_config_t *cfg, int new_mode)
{
	LOG1("Entering listen mode: %s",
		new_mode != LISTEN_NONE
			? (new_mode == LISTEN_KERNEL ? "kernel" : "raw")
			: "none"
	);

	listen_mode = new_mode;
	if (sockfd >= 0) {
		close(sockfd);
		sockfd = -1;
	}
	if (new_mode == LISTEN_KERNEL)
		sockfd = udhcp_listen_socket(/*INADDR_ANY,*/ CLIENT_PORT, cfg->interface);
	else if (new_mode != LISTEN_NONE)
		sockfd = udhcp_raw_socket(cfg->ifindex);
	/* else LISTEN_NONE: sockfd stays closed */
}

static void perform_renew(struct client_config_t *cfg)
{
	NOTE("Performing a DHCP renew");
	switch (state) {
	case BOUND:
		change_listen_mode(cfg, LISTEN_KERNEL);
	case RENEWING:
	case REBINDING:
		state = RENEW_REQUESTED;
		break;
	case RENEW_REQUESTED: /* impatient are we? fine, square 1 */
		udhcp_run_script(NULL, "deconfig");
	case REQUESTING:
	case RELEASED:
		change_listen_mode(cfg, LISTEN_RAW);
		state = INIT_SELECTING;
		break;
	case INIT_SELECTING:
		break;
	}
}

static void perform_release(struct client_config_t *cfg, uint32_t requested_ip, uint32_t server_addr)
{
	char buffer[sizeof("255.255.255.255")];
	struct in_addr temp_addr;

	/* send release packet */
	if (state == BOUND || state == RENEWING || state == REBINDING) {
		temp_addr.s_addr = server_addr;
		strcpy(buffer, (char *)inet_ntoa(temp_addr));
		temp_addr.s_addr = requested_ip;
		NOTE("Unicasting a release of %s to %s",
				inet_ntoa(temp_addr), buffer);
		send_release(cfg, server_addr, requested_ip); /* unicast */
		udhcp_run_script(NULL, "deconfig");
	}
	NOTE("Entering released state");

	change_listen_mode(cfg, LISTEN_NONE);
	state = RELEASED;
}

static uint8_t* alloc_dhcp_option(int code, const char *str, int extra)
{
	uint8_t *storage;
	int len = strnlen(str, 255);
	storage = xzalloc(len + extra + OPT_DATA);
	storage[OPT_CODE] = code;
	storage[OPT_LEN] = len + extra;
	memcpy(storage + extra + OPT_DATA, str, len);
	return storage;
}

static int client_background(void)
{
	int pid;
	int fd;

	pid = fork();
	if (pid)
		return pid;

	chdir("/");

	close(0);
	close(1);
	close(2);

        fd = open("/dev/null", O_RDWR);
        if (fd < 0) {
                fd = open("/", O_RDONLY); /* don't believe this can fail */
        }

        while ((unsigned)fd < 2)
                fd = dup(fd); /* have 0,1,2 open at least to /dev/null */

	setsid();
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	return 0;
}



static int udhcp_read_interface(const char *interface, int *ifindex, uint32_t *nip, uint8_t *mac)
{
	int fd;
	struct ifreq ifr;
	struct sockaddr_in *our_ip;

	memset(&ifr, 0, sizeof(ifr));
	fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

	ifr.ifr_addr.sa_family = AF_INET;
	strncpy_IFNAMSIZ(ifr.ifr_name, interface);
	if (nip) {
		if (-1 == ioctl(fd, SIOCGIFADDR, &ifr)) {
			perror("is interface up and configured?");
			close(fd);
			return -1;
		}
		our_ip = (struct sockaddr_in *) &ifr.ifr_addr;
		*nip = our_ip->sin_addr.s_addr;
		LOG1("IP %s", inet_ntoa(our_ip->sin_addr));
	}

	if (ifindex) {
		if (-1 == ioctl(fd, SIOCGIFINDEX, &ifr)) {
			ERROR("Unable to get index");
			close(fd);
			return -1;
		}
		LOG1("Adapter index %d", ifr.ifr_ifindex);
		*ifindex = ifr.ifr_ifindex;
	}

	if (mac) {
		if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) {
			ERROR("Unable to get HW addr");
			close(fd);
			return -1;
		}
		memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
		LOG1("MAC %02x:%02x:%02x:%02x:%02x:%02x",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	close(fd);
	return 0;
}

/* 1. None of the callers expects it to ever fail */
/* 2. ip was always INADDR_ANY */
static int udhcp_listen_socket(/*uint32_t ip,*/ int port, const char *inf)
{
	int fd;
	struct sockaddr_in addr;

	LOG1("Opening listen socket on *:%d %s", port, inf);
	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	setsockopt_reuseaddr(fd);
	if (setsockopt_broadcast(fd) == -1)
		ERROR("SO_BROADCAST");

	/* NB: bug 1032 says this doesn't work on ethernet aliases (ethN:M) */
	if (setsockopt_bindtodevice(fd, inf))
		exit(1); /* warning is already printed */

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	/* addr.sin_addr.s_addr = ip; - all-zeros is INADDR_ANY */
	bind(fd, (struct sockaddr *)&addr, sizeof(addr));

	return fd;
}


static void udhcp_init_header(struct dhcp_packet *packet, char type)
{
	memset(packet, 0, sizeof(*packet));
	packet->op = BOOTREQUEST; /* if client to a server */
	switch (type) {
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		packet->op = BOOTREPLY; /* if server to client */
	}
	packet->htype = 1; /* ethernet */
	packet->hlen = 6;
	packet->cookie = htonl(DHCP_MAGIC);
	if (DHCP_END != 0)
		packet->options[0] = DHCP_END;
	udhcp_add_simple_option(packet, DHCP_MESSAGE_TYPE, type);
}

/* Get an option with bounds checking (warning, result is not aligned) */
static uint8_t* udhcp_get_option(struct dhcp_packet *packet, int code) 
{ 
        uint8_t *optionptr; 
        int len; 
        int rem; 
        int overload = 0; 
        enum {  
                FILE_FIELD101  = FILE_FIELD  * 0x101, 
                SNAME_FIELD101 = SNAME_FIELD * 0x101, 
        }; 
 
        /* option bytes: [code][len][data1][data2]..[dataLEN] */ 
        optionptr = packet->options; 
        rem = sizeof(packet->options); 
        while (1) { 
                if (rem <= 0) { 
                        ERROR("bad packet, malformed option field"); 
                        return NULL; 
                } 
                if (optionptr[OPT_CODE] == DHCP_PADDING) { 
                        rem--; 
                        optionptr++; 
                        continue; 
                } 
                if (optionptr[OPT_CODE] == DHCP_END) { 
                        if ((overload & FILE_FIELD101) == FILE_FIELD) { 
                                /* can use packet->file, and didn't look at it yet */ 
                                overload |= FILE_FIELD101; /* "we looked at it" */ 
                                optionptr = packet->file; 
                                rem = sizeof(packet->file); 
                                continue; 
                        } 
                        if ((overload & SNAME_FIELD101) == SNAME_FIELD) { 
                                /* can use packet->sname, and didn't look at it yet */ 
                                overload |= SNAME_FIELD101; /* "we looked at it" */ 
                                optionptr = packet->sname; 
                                rem = sizeof(packet->sname); 
                                continue; 
                        } 
                        break; 
                } 
                len = 2 + optionptr[OPT_LEN]; 
                rem -= len; 
                if (rem < 0) 
                        continue; /* complain and return NULL */ 
 
                if (optionptr[OPT_CODE] == code) { 
                        return optionptr + OPT_DATA; 
                } 
 
                if (optionptr[OPT_CODE] == DHCP_OPTION_OVERLOAD) { 
                        overload |= optionptr[OPT_DATA]; 
                        /* fall through */ 
                } 
                optionptr += len; 
        } 
 
        return NULL; 
} 


/* Read a packet from socket fd, return -1 on read error, -2 on packet error */
static int udhcp_recv_kernel_packet(struct dhcp_packet *packet, int fd)
{
	int bytes;
	unsigned char *vendor;

	memset(packet, 0, sizeof(*packet));
	bytes = read(fd, packet, sizeof(*packet));
	if (bytes < 0) {
		LOG1("Packet read error, ignoring");
		return bytes; /* returns -1 */
	}

	if (packet->cookie != htonl(DHCP_MAGIC)) {
		NOTE("Packet with bad magic, ignoring");
		return -2;
	}
	LOG1("Received a packet");

	if (packet->op == BOOTREQUEST) {
		vendor = udhcp_get_option(packet, DHCP_VENDOR);
		if (vendor) {
#if 0
			static const char broken_vendors[][8] = {
				"MSFT 98",
				""
			};
			int i;
			for (i = 0; broken_vendors[i][0]; i++) {
				if (vendor[OPT_LEN - OPT_DATA] == (uint8_t)strlen(broken_vendors[i])
				 && strncmp((char*)vendor, broken_vendors[i], vendor[OPT_LEN - OPT_DATA]) == 0
				) {
					LOG1("Broken client (%s), forcing broadcast replies",
						broken_vendors[i]);
					packet->flags |= htons(BROADCAST_FLAG);
				}
			}
#else
			if (vendor[OPT_LEN - OPT_DATA] == (uint8_t)(sizeof("MSFT 98")-1)
			 && memcmp(vendor, "MSFT 98", sizeof("MSFT 98")-1) == 0
			) {
				LOG1("Broken client (%s), forcing broadcast replies", "MSFT 98");
				packet->flags |= htons(BROADCAST_FLAG);
			}
#endif
		}
	}

	return bytes;
}

static uint16_t udhcp_checksum(void *addr, int count)
{
	/* Compute Internet Checksum for "count" bytes
	 * beginning at location "addr".
	 */
	int32_t sum = 0;
	uint16_t *source = (uint16_t *) addr;

	while (count > 1)  {
		/*  This is the inner loop */
		sum += *source++;
		count -= 2;
	}

	/*  Add left-over byte, if any */
	if (count > 0) {
		/* Make sure that the left-over byte is added correctly both
		 * with little and big endian hosts */
		uint16_t tmp = 0;
		*(uint8_t*)&tmp = *(uint8_t*)source;
		sum += tmp;
	}
	/*  Fold 32-bit sum to 16 bits */
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

/* Construct a ip/udp header for a packet, send packet */
static int udhcp_send_raw_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port, const uint8_t *dest_arp,
		int ifindex)
{
	struct sockaddr_ll dest_sll;
	struct ip_udp_dhcp_packet packet;
	unsigned padding;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
	if (fd < 0) {
		ERROR("socket(%s)", "PACKET");
		return result;
	}

	memset(&dest_sll, 0, sizeof(dest_sll));
	memset(&packet, 0, offsetof(struct ip_udp_dhcp_packet, data));
	packet.data = *dhcp_pkt; /* struct copy */

	dest_sll.sll_family = AF_PACKET;
	dest_sll.sll_protocol = htons(ETH_P_IP);
	dest_sll.sll_ifindex = ifindex;
	dest_sll.sll_halen = 6;
	memcpy(dest_sll.sll_addr, dest_arp, 6);

	if (bind(fd, (struct sockaddr *)&dest_sll, sizeof(dest_sll)) < 0) {
		msg = "bind(%s)";
		goto ret_close;
	}

	/* We were sending full-sized DHCP packets (zero padded),
	 * but some badly configured servers were seen dropping them.
	 * Apparently they drop all DHCP packets >576 *ethernet* octets big,
	 * whereas they may only drop packets >576 *IP* octets big
	 * (which for typical Ethernet II means 590 octets: 6+6+2 + 576).
	 *
	 * In order to work with those buggy servers,
	 * we truncate packets after end option byte.
	 */
	padding = DHCP_OPTIONS_BUFSIZE - 1 - udhcp_end_option(packet.data.options);

	packet.ip.protocol = IPPROTO_UDP;
	packet.ip.saddr = source_nip;
	packet.ip.daddr = dest_nip;
	packet.udp.source = htons(source_port);
	packet.udp.dest = htons(dest_port);
	/* size, excluding IP header: */
	packet.udp.len = htons(UDP_DHCP_SIZE - padding);
	/* for UDP checksumming, ip.len is set to UDP packet len */
	packet.ip.tot_len = packet.udp.len;
	packet.udp.check = udhcp_checksum(&packet, IP_UDP_DHCP_SIZE - padding);
	/* but for sending, it is set to IP packet len */
	packet.ip.tot_len = htons(IP_UDP_DHCP_SIZE - padding);
	packet.ip.ihl = sizeof(packet.ip) >> 2;
	packet.ip.version = IPVERSION;
	packet.ip.ttl = IPDEFTTL;
	packet.ip.check = udhcp_checksum(&packet.ip, sizeof(packet.ip));

	result = sendto(fd, &packet, IP_UDP_DHCP_SIZE - padding, /*flags:*/ 0,
			(struct sockaddr *) &dest_sll, sizeof(dest_sll));
	msg = "sendto";
 ret_close:
	close(fd);
	if (result < 0) {
		ERROR("Error %s for PACKET", msg);
	}
	return result;
}

/* Let the kernel do all the work for packet generation */
static int udhcp_send_kernel_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_nip, int source_port,
		uint32_t dest_nip, int dest_port)
{
	struct sockaddr_in client;
	unsigned padding;
	int fd;
	int result = -1;
	const char *msg;

	fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		msg = "socket(%s)";
		goto ret_msg;
	}
	setsockopt_reuseaddr(fd);

	memset(&client, 0, sizeof(client));
	client.sin_family = AF_INET;
	client.sin_port = htons(source_port);
	client.sin_addr.s_addr = source_nip;
	if (bind(fd, (struct sockaddr *)&client, sizeof(client)) == -1) {
		msg = "bind(%s)";
		goto ret_close;
	}

	memset(&client, 0, sizeof(client));
	client.sin_family = AF_INET;
	client.sin_port = htons(dest_port);
	client.sin_addr.s_addr = dest_nip;
	if (connect(fd, (struct sockaddr *)&client, sizeof(client)) == -1) {
		msg = "connect";
		goto ret_close;
	}


	padding = DHCP_OPTIONS_BUFSIZE - 1 - udhcp_end_option(dhcp_pkt->options);
	result = write(fd, dhcp_pkt, DHCP_SIZE - padding);
	msg = "write";
 ret_close:
	close(fd);
	if (result < 0) {
 ret_msg:
		ERROR("Error doing %s for %s", msg, "UDP");
	}
	return result;
}

static struct fd_pair {
	int rd;
	int wr;
} signal_pipe;

static void signal_handler(int sig)
{
        unsigned char ch = sig; /* use char, avoid dealing with partial writes */
        if (write(signal_pipe.wr, &ch, 1) != 1)
                ERROR("can't send signal");
}

/* Call this before doing anything else. Sets up the socket pair
 * and installs the signal handler */
static void udhcp_sp_setup(void)
{
        /* was socketpair, but it needs AF_UNIX in kernel */
        pipe((int *)&signal_pipe);
	fcntl(signal_pipe.rd, F_SETFD, FD_CLOEXEC);
	fcntl(signal_pipe.wr, F_SETFD, FD_CLOEXEC);
	fcntl(signal_pipe.wr, F_SETFL, fcntl(signal_pipe.wr, F_GETFL) | O_NONBLOCK);
	signal(SIGUSR1, signal_handler);
	signal(SIGUSR2, signal_handler);
	signal(SIGTERM, signal_handler);
}

int udhcp_sp_fd_set(fd_set *rfds, int extra_fd)
{
        FD_ZERO(rfds);
        FD_SET(signal_pipe.rd, rfds);
        if (extra_fd >= 0) {
		fcntl(extra_fd, F_SETFD, FD_CLOEXEC);
                FD_SET(extra_fd, rfds);
        }
        return signal_pipe.rd > extra_fd ? signal_pipe.rd : extra_fd;
}

/* Read a signal from the signal pipe. Returns 0 if there is
 * no signal, -1 on error (and sets errno appropriately), and
 * your signal on success */
static int udhcp_sp_read(const fd_set *rfds)
{
        unsigned char sig;

        if (!FD_ISSET(signal_pipe.rd, rfds))
                return 0;

        if (read(signal_pipe.rd, &sig, 1) != 1)
                return -1;

        return sig;
}


int udhcpc_main(char *interface)
{
        struct client_config_t client_config;
	uint8_t *temp, *message;
	int discover_timeout = 3;
	int discover_retries = 3;
	uint32_t server_addr = server_addr; /* for compiler */
	uint32_t requested_ip = 0;
	uint32_t xid = 0;
	uint32_t lease_seconds = 0; /* can be given as 32-bit quantity */
	int packet_num;
	int timeout; /* must be signed */
	unsigned already_waited_sec;
	int max_fd;
	int retval;
	struct timeval tv;
	struct dhcp_packet packet;
	fd_set rfds;
	int pid = 0;

	bzero(&client_config, sizeof(client_config));

	/* Default options */
	client_config.interface = interface;
	client_config.hostname = alloc_dhcp_option(DHCP_HOST_NAME, "NeTV-Recovery", 0);

	if (udhcp_read_interface(client_config.interface,
			&client_config.ifindex,
			NULL,
			client_config.client_mac)
	) {
		return 1;
	}

	/* Set up the signal pipe */
	udhcp_sp_setup();
	/* We want random_xid to be random... */
	srand(monotonic_us());

	state = INIT_SELECTING;
	udhcp_run_script(NULL, "deconfig");
	change_listen_mode(&client_config, LISTEN_RAW);
	packet_num = 0;
	timeout = 0;
	already_waited_sec = 0;

	/* Main event loop. select() waits on signal pipe and possibly
	 * on sockfd.
	 * "continue" statements in code below jump to the top of the loop.
	 */
	for (;;) {
		/* silence "uninitialized!" warning */
		unsigned timestamp_before_wait = timestamp_before_wait;

		//ERROR("sockfd:%d, listen_mode:%d", sockfd, listen_mode);

		/* Was opening raw or udp socket here
		 * if (listen_mode != LISTEN_NONE && sockfd < 0),
		 * but on fast network renew responses return faster
		 * than we open sockets. Thus this code is moved
		 * to change_listen_mode(). Thus we open listen socket
		 * BEFORE we send renew request (see "case BOUND:"). */

		max_fd = udhcp_sp_fd_set(&rfds, sockfd);

		tv.tv_sec = timeout - already_waited_sec;
		tv.tv_usec = 0;
		retval = 0;
		/* If we already timed out, fall through with retval = 0, else... */
		if ((int)tv.tv_sec > 0) {
			timestamp_before_wait = (unsigned)monotonic_sec();
			LOG1("Waiting on select...");
			retval = select(max_fd + 1, &rfds, NULL, NULL, &tv);
			if (retval < 0) {
				/* EINTR? A signal was caught, don't panic */
				if (errno == EINTR) {
					already_waited_sec += (unsigned)monotonic_sec() - timestamp_before_wait;
					continue;
				}
				/* Else: an error occured, panic! */
				ERROR("select");
			}
		}

		/* If timeout dropped to zero, time to become active:
		 * resend discover/renew/whatever
		 */
		if (retval == 0) {
			/* When running on a bridge, the ifindex may have changed
			 * (e.g. if member interfaces were added/removed
			 * or if the status of the bridge changed).
			 * Refresh ifindex and client_mac:
			 */
			if (udhcp_read_interface(client_config.interface,
					&client_config.ifindex,
					NULL,
					client_config.client_mac)
			) {
				return 1; /* iface is gone? */
			}

			/* We will restart the wait in any case */
			already_waited_sec = 0;

			switch (state) {
			case INIT_SELECTING:
				if (packet_num < discover_retries) {
					if (packet_num == 0)
						xid = random_xid();
					/* broadcast */
					send_discover(&client_config, xid, requested_ip);
					timeout = discover_timeout;
					packet_num++;
					continue;
				}
 leasefail:
				udhcp_run_script(NULL, "leasefail");
				NOTE("No lease, failing");
				retval = -1;
				goto ret;
			case REQUESTING:
				if (packet_num < discover_retries) {
					/* send broadcast select packet */
					send_select(&client_config, xid, server_addr, requested_ip);
					timeout = discover_timeout;
					packet_num++;
					continue;
				}
				/* Timed out, go back to init state.
				 * "discover...select...discover..." loops
				 * were seen in the wild. Treat them similarly
				 * to "no response to discover" case */
				change_listen_mode(&client_config, LISTEN_RAW);
				state = INIT_SELECTING;
				goto leasefail;
			case BOUND:
				/* 1/2 lease passed, enter renewing state */
				state = RENEWING;
				change_listen_mode(&client_config, LISTEN_KERNEL);
				LOG1("Entering renew state");
				/* fall right through */
			case RENEW_REQUESTED: /* manual (SIGUSR1) renew */
			case_RENEW_REQUESTED:
			case RENEWING:
				if (timeout > 60) {
					/* send an unicast renew request */
			/* Sometimes observed to fail (EADDRNOTAVAIL) to bind
			 * a new UDP socket for sending inside send_renew.
			 * I hazard to guess existing listening socket
			 * is somehow conflicting with it, but why is it
			 * not deterministic then?! Strange.
			 * Anyway, it does recover by eventually failing through
			 * into INIT_SELECTING state.
			 */
					send_renew(&client_config, xid, server_addr, requested_ip);
					timeout >>= 1;
					continue;
				}
				/* Timed out, enter rebinding state */
				LOG1("Entering rebinding state");
				state = REBINDING;
				/* fall right through */
			case REBINDING:
				/* Switch to bcast receive */
				change_listen_mode(&client_config, LISTEN_RAW);
				/* Lease is *really* about to run out,
				 * try to find DHCP server using broadcast */
				if (timeout > 0) {
					/* send a broadcast renew request */
					send_renew(&client_config, xid, 0 /*INADDR_ANY*/, requested_ip);
					timeout >>= 1;
					continue;
				}
				/* Timed out, enter init state */
				NOTE("Lease lost, entering init state");
				udhcp_run_script(NULL, "deconfig");
				state = INIT_SELECTING;
				/*timeout = 0; - already is */
				packet_num = 0;
				continue;
			/* case RELEASED: */
			}
			/* yah, I know, *you* say it would never happen */
			timeout = INT_MAX;
			continue; /* back to main loop */
		} /* if select timed out */

		/* select() didn't timeout, something happened */

		/* Is it a signal? */
		/* note: udhcp_sp_read checks FD_ISSET before reading */
		switch (udhcp_sp_read(&rfds)) {
		case SIGUSR1:
			perform_renew(&client_config);
			if (state == RENEW_REQUESTED)
				goto case_RENEW_REQUESTED;
			/* Start things over */
			packet_num = 0;
			/* Kill any timeouts, user wants this to hurry along */
			timeout = 0;
			continue;
		case SIGUSR2:
			perform_release(&client_config, requested_ip, server_addr);
			timeout = INT_MAX;
			continue;
		case SIGTERM:
			NOTE("Received SIGTERM");
			perform_release(&client_config, requested_ip, server_addr);
			goto ret0;
		}

		/* Is it a packet? */
		if (listen_mode == LISTEN_NONE || !FD_ISSET(sockfd, &rfds))
			continue; /* no */

		{
			int len;

			/* A packet is ready, read it */
			if (listen_mode == LISTEN_KERNEL)
				len = udhcp_recv_kernel_packet(&packet, sockfd);
			else
				len = udhcp_recv_raw_packet(&packet, sockfd);
			if (len == -1) {
				/* Error is severe, reopen socket */
				NOTE("Read error: %s, reopening socket", strerror(errno));
				sleep(discover_timeout); /* 3 seconds by default */
				change_listen_mode(&client_config, listen_mode); /* just close and reopen */
			}
			/* If this packet will turn out to be unrelated/bogus,
			 * we will go back and wait for next one.
			 * Be sure timeout is properly decreased. */
			already_waited_sec += (unsigned)monotonic_sec() - timestamp_before_wait;
			if (len < 0)
				continue;
		}

		if (packet.xid != xid) {
			LOG1("xid %x (our is %x), ignoring packet",
				(unsigned)packet.xid, (unsigned)xid);
			continue;
		}

		/* Ignore packets that aren't for us */
		if (packet.hlen != 6
		 || memcmp(packet.chaddr, client_config.client_mac, 6) != 0
		) {
//FIXME: need to also check that last 10 bytes are zero
			LOG1("chaddr does not match, ignoring packet"); // log2?
			continue;
		}

		message = (unsigned char *)udhcp_get_option(&packet, DHCP_MESSAGE_TYPE);
		if (message == NULL) {
			ERROR("no message type option, ignoring packet");
			continue;
		}

		switch (state) {
		case INIT_SELECTING:
			/* Must be a DHCPOFFER to one of our xid's */
			if (*message == DHCPOFFER) {
		/* TODO: why we don't just fetch server's IP from IP header? */
				temp = (unsigned char *)udhcp_get_option(&packet, DHCP_SERVER_ID);
				if (!temp) {
					ERROR("no server ID, ignoring packet");
					continue;
					/* still selecting - this server looks bad */
				}
				/* it IS unaligned sometimes, don't "optimize" */
				move_from_unaligned32(server_addr, temp);
				/*xid = packet.xid; - already is */
				requested_ip = packet.yiaddr;

				/* enter requesting state */
				state = REQUESTING;
				timeout = 0;
				packet_num = 0;
				already_waited_sec = 0;
			}
			continue;
		case REQUESTING:
		case RENEWING:
		case RENEW_REQUESTED:
		case REBINDING:
			if (*message == DHCPACK) {
				temp = (unsigned char *)udhcp_get_option(&packet, DHCP_LEASE_TIME);
				if (!temp) {
					ERROR("no lease time with ACK, using 1 hour lease");
					lease_seconds = 60 * 60;
				} else {
					/* it IS unaligned sometimes, don't "optimize" */
					move_from_unaligned32(lease_seconds, temp);
					lease_seconds = ntohl(lease_seconds);
					lease_seconds &= 0x0fffffff; /* paranoia: must not be prone to overflows */
					if (lease_seconds < 10) /* and not too small */
						lease_seconds = 10;
				}
				/* enter bound state */
				timeout = lease_seconds / 2;
				{
					struct in_addr temp_addr;
					temp_addr.s_addr = packet.yiaddr;
					NOTE("Lease of %s obtained, lease time %u",
						inet_ntoa(temp_addr), (unsigned)lease_seconds);
				}
				requested_ip = packet.yiaddr;
				udhcp_run_script(&packet, state == REQUESTING ? "bound" : "renew");

				state = BOUND;
				change_listen_mode(&client_config, LISTEN_NONE);
				if ((pid=client_background()))
					return pid;
				/* do not background again! */
				already_waited_sec = 0;
				continue; /* back to main loop */
			}
			if (*message == DHCPNAK) {
				/* return to init state */
				NOTE("Received DHCP NAK");
				udhcp_run_script(&packet, "nak");
				if (state != REQUESTING)
					udhcp_run_script(NULL, "deconfig");
				change_listen_mode(&client_config, LISTEN_RAW);
				sleep(3); /* avoid excessive network traffic */
				state = INIT_SELECTING;
				requested_ip = 0;
				timeout = 0;
				packet_num = 0;
				already_waited_sec = 0;
			}
			continue;
		/* case BOUND: - ignore all packets */
		/* case RELEASED: - ignore all packets */
		}
		/* back to main loop */
	} /* for (;;) - main loop ends */

 ret0:
	retval = 0;
 ret:
	return retval;
}
