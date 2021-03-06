#include "ap-scan.h"

#if defined(linux) && !defined(SIMULATION)
/*
 *      Wireless Tools
 *
 *              Jean II - HPLB '99 - HPL 99->07
 *
 * This tool can access various piece of information on the card
 * not part of iwconfig...
 * You need to link this code against "iwlist.c" and "-lm".
 *
 * This file is released under the GPL license.
 *     Copyright (c) 1997-2007 Jean Tourrilhes <jt@hpl.hp.com>
 */

#include <sys/time.h>
#include <net/ethernet.h>   /* struct ether_addr */
#include "iwlib.h"              /* Header */

#define DEFAULT_DEVICE "wlan0"

static struct ap_description *found_aps = NULL;
static int found_ap_count = 0;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define IEEE80211_ELEMID_SSID                   0
#define IEEE80211_ELEMID_RSN 			48
#define IEEE80211_ELEMID_VENDOR 		221
#define RSN_OUI                 0xac0f00

#define RSN_ASE_8021X_NONE      0x00
#define RSN_ASE_8021X_UNSPEC    0x01
#define RSN_ASE_8021X_PSK       0x02

#define RSN_CSE_NULL            0x00
#define RSN_CSE_WEP40           0x01
#define RSN_CSE_TKIP            0x02
#define RSN_CSE_WRAP            0x03
#define RSN_CSE_CCMP            0x04
#define RSN_CSE_WEP104          0x05

#define RSN_VERSION             1 

#define WPA_OUI                 0xf25000
#define WPA_OUI_TYPE            0x01
#define WPA_VERSION             1

#define 	min_c(a, b)   ((a)>(b)?(b):(a))





//#define SWAB_16(x) (((((uint32_t)x)&0xff)<<8) | ((((uint32_t)x)&0xff00)>>8))
#define SWAB_16(x) x


const char * const iw_operation_mode[] = {
    "Auto",
    "Ad-Hoc",
    "Managed",
    "Master",
    "Repeater",
    "Secondary",
    "Monitor",
    "Unknown/bug"
};

/*
static const char *chumby_encryptions[] = {
    "NONE",
    "WEP",
    "AES",
    "TKIP",
};

static const char *chumby_auths[] = {
    "OPEN",
    "WEPAUTO",
    "WPA2PSK",
    "WPAPSK",
    "WPA2EAP",
    "WPAEAP",
};
*/



/**************************** CONSTANTS ****************************/

#define IW_SCAN_HACK            0x8000



/****************************** STOLEN ******************************/

int
iw_sockets_open(void)
{     
  static const int families[] = {
    AF_INET, AF_IPX, AF_AX25, AF_APPLETALK
  };
  unsigned int  i;
  int       sock;
      
  /*
   * Now pick any (exisiting) useful socket family for generic queries
   * Note : don't open all the socket, only returns when one matches,
   * all protocols might not be valid.
   * Workaround by Jim Kaba <jkaba@sarnoff.com>
   * Note : in 99% of the case, we will just open the inet_sock.
   * The remaining 1% case are not fully correct...
   */    
      
  /* Try all families we support */
  for(i = 0; i < sizeof(families)/sizeof(int); ++i)
    { 
      /* Try to open the socket, if success returns it */
      sock = socket(families[i], SOCK_DGRAM, 0);
      if(sock >= 0)
    return sock;
  }   

  return -1;
}



/************************ EVENT SUBROUTINES ************************/
/*
 * The Wireless Extension API 14 and greater define Wireless Events,
 * that are used for various events and scanning.
 * Those functions help the decoding of events, so are needed only in
 * this case.
 */

/* -------------------------- CONSTANTS -------------------------- */

/* Type of headers we know about (basically union iwreq_data) */
#define IW_HEADER_TYPE_NULL 0   /* Not available */
#define IW_HEADER_TYPE_CHAR 2   /* char [IFNAMSIZ] */
#define IW_HEADER_TYPE_UINT 4   /* __u32 */
#define IW_HEADER_TYPE_FREQ 5   /* struct iw_freq */
#define IW_HEADER_TYPE_ADDR 6   /* struct sockaddr */
#define IW_HEADER_TYPE_POINT    8   /* struct iw_point */
#define IW_HEADER_TYPE_PARAM    9   /* struct iw_param */
#define IW_HEADER_TYPE_QUAL 10  /* struct iw_quality */

/* Handling flags */
/* Most are not implemented. I just use them as a reminder of some
 * cool features we might need one day ;-) */
#define IW_DESCR_FLAG_NONE  0x0000  /* Obvious */
/* Wrapper level flags */
#define IW_DESCR_FLAG_DUMP  0x0001  /* Not part of the dump command */
#define IW_DESCR_FLAG_EVENT 0x0002  /* Generate an event on SET */
#define IW_DESCR_FLAG_RESTRICT  0x0004  /* GET : request is ROOT only */
                /* SET : Omit payload from generated iwevent */
#define IW_DESCR_FLAG_NOMAX 0x0008  /* GET : no limit on request size */
/* Driver level flags */
#define IW_DESCR_FLAG_WAIT  0x0100  /* Wait for driver event */

/* ---------------------------- TYPES ---------------------------- */

/*
 * Describe how a standard IOCTL looks like.
 */
struct iw_ioctl_description
{
    __u8    header_type;        /* NULL, iw_point or other */
    __u8    token_type;     /* Future */
    __u16   token_size;     /* Granularity of payload */
    __u16   min_tokens;     /* Min acceptable token number */
    __u16   max_tokens;     /* Max acceptable token number */
    __u32   flags;          /* Special handling of the request */
};

/* ie_data taken from Haiku's NetworkDevice.cpp */

struct ie_data {
	uint8_t	type;
	uint8_t	length;
	uint8_t	data[1];
};

/* -------------------------- VARIABLES -------------------------- */

/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct iw_ioctl_description standard_ioctl_descr[] = {
    [SIOCSIWCOMMIT  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_NULL,
    },
    [SIOCGIWNAME    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_CHAR,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWNWID    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
        .flags      = IW_DESCR_FLAG_EVENT,
    },
    [SIOCGIWNWID    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWFREQ    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_FREQ,
        .flags      = IW_DESCR_FLAG_EVENT,
    },
    [SIOCGIWFREQ    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_FREQ,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWMODE    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_UINT,
        .flags      = IW_DESCR_FLAG_EVENT,
    },
    [SIOCGIWMODE    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_UINT,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWSENS    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWSENS    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRANGE   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_NULL,
    },
    [SIOCGIWRANGE   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = sizeof(struct iw_range),
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWPRIV    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_NULL,
    },
    [SIOCGIWPRIV    - SIOCIWFIRST] = { /* (handled directly by us) */
        .header_type    = IW_HEADER_TYPE_NULL,
    },
    [SIOCSIWSTATS   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_NULL,
    },
    [SIOCGIWSTATS   - SIOCIWFIRST] = { /* (handled directly by us) */
        .header_type    = IW_HEADER_TYPE_NULL,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWSPY - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr),
        .max_tokens = IW_MAX_SPY,
    },
    [SIOCGIWSPY - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr) +
                  sizeof(struct iw_quality),
        .max_tokens = IW_MAX_SPY,
    },
    [SIOCSIWTHRSPY  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = sizeof(struct iw_thrspy),
        .min_tokens = 1,
        .max_tokens = 1,
    },
    [SIOCGIWTHRSPY  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = sizeof(struct iw_thrspy),
        .min_tokens = 1,
        .max_tokens = 1,
    },
    [SIOCSIWAP  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_ADDR,
    },
    [SIOCGIWAP  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_ADDR,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWMLME    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_mlme),
        .max_tokens = sizeof(struct iw_mlme),
    },
    [SIOCGIWAPLIST  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = sizeof(struct sockaddr) +
                  sizeof(struct iw_quality),
        .max_tokens = IW_MAX_AP,
        .flags      = IW_DESCR_FLAG_NOMAX,
    },
    [SIOCSIWSCAN    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = 0,
        .max_tokens = sizeof(struct iw_scan_req),
    },
    [SIOCGIWSCAN    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_SCAN_MAX_DATA,
        .flags      = IW_DESCR_FLAG_NOMAX,
    },
    [SIOCSIWESSID   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
        .flags      = IW_DESCR_FLAG_EVENT,
    },
    [SIOCGIWESSID   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
        .flags      = IW_DESCR_FLAG_DUMP,
    },
    [SIOCSIWNICKN   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
    },
    [SIOCGIWNICKN   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ESSID_MAX_SIZE + 1,
    },
    [SIOCSIWRATE    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRATE    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRTS - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRTS - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWFRAG    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWFRAG    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWTXPOW   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWTXPOW   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWRETRY   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWRETRY   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWENCODE  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ENCODING_TOKEN_MAX,
        .flags      = IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT,
    },
    [SIOCGIWENCODE  - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_ENCODING_TOKEN_MAX,
        .flags      = IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT,
    },
    [SIOCSIWPOWER   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWPOWER   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWMODUL   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWMODUL   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWGENIE   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [SIOCGIWGENIE   - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [SIOCSIWAUTH    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCGIWAUTH    - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_PARAM,
    },
    [SIOCSIWENCODEEXT - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_encode_ext),
        .max_tokens = sizeof(struct iw_encode_ext) +
                  IW_ENCODING_TOKEN_MAX,
    },
    [SIOCGIWENCODEEXT - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_encode_ext),
        .max_tokens = sizeof(struct iw_encode_ext) +
                  IW_ENCODING_TOKEN_MAX,
    },
    [SIOCSIWPMKSA - SIOCIWFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .min_tokens = sizeof(struct iw_pmksa),
        .max_tokens = sizeof(struct iw_pmksa),
    },
};
static const unsigned int standard_ioctl_num = (sizeof(standard_ioctl_descr) /
                        sizeof(struct iw_ioctl_description));

/*
 * Meta-data about all the additional standard Wireless Extension events
 * we know about.
 */
static const struct iw_ioctl_description standard_event_descr[] = {
    [IWEVTXDROP - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_ADDR,
    },
    [IWEVQUAL   - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_QUAL,
    },
    [IWEVCUSTOM - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_CUSTOM_MAX,
    },
    [IWEVREGISTERED - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_ADDR,
    },
    [IWEVEXPIRED    - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_ADDR, 
    },
    [IWEVGENIE  - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVMICHAELMICFAILURE  - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT, 
        .token_size = 1,
        .max_tokens = sizeof(struct iw_michaelmicfailure),
    },
    [IWEVASSOCREQIE - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVASSOCRESPIE    - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = IW_GENERIC_IE_MAX,
    },
    [IWEVPMKIDCAND  - IWEVFIRST] = {
        .header_type    = IW_HEADER_TYPE_POINT,
        .token_size = 1,
        .max_tokens = sizeof(struct iw_pmkid_cand),
    },
};
static const unsigned int standard_event_num = (sizeof(standard_event_descr) /
                        sizeof(struct iw_ioctl_description));

/* Size (in bytes) of various events */
static const int event_type_size[] = {
    IW_EV_LCP_PK_LEN,   /* IW_HEADER_TYPE_NULL */
    0,
    IW_EV_CHAR_PK_LEN,  /* IW_HEADER_TYPE_CHAR */
    0,
    IW_EV_UINT_PK_LEN,  /* IW_HEADER_TYPE_UINT */
    IW_EV_FREQ_PK_LEN,  /* IW_HEADER_TYPE_FREQ */
    IW_EV_ADDR_PK_LEN,  /* IW_HEADER_TYPE_ADDR */
    0,
    IW_EV_POINT_PK_LEN, /* Without variable payload */
    IW_EV_PARAM_PK_LEN, /* IW_HEADER_TYPE_PARAM */
    IW_EV_QUAL_PK_LEN,  /* IW_HEADER_TYPE_QUAL */
};



int
iw_extract_event_stream(struct stream_descr *stream, /* Stream of events */
                        struct iw_event     *iwe     /* Extracted event */
) {
    const struct iw_ioctl_description *descr = NULL;
    int                                event_type = 0;
    unsigned int                       event_len  = 1;      /* Invalid */
    char                              *pointer;
    /* Don't "optimise" the following variable, it will crash */
    unsigned                           cmd_index;      /* *MUST* be unsigned */

    /* Check for end of stream */
    if((stream->current + IW_EV_LCP_PK_LEN) > stream->end)
        return(0);

#ifdef DEBUG
    printf("DBG - stream->current = %p, stream->value = %p, stream->end = %p\n",
        stream->current, stream->value, stream->end);
#endif

    /* Extract the event header (to get the event id).
     * Note : the event may be unaligned, therefore copy... */
    memcpy((char *) iwe, stream->current, IW_EV_LCP_PK_LEN);

#ifdef DEBUG
    printf("DBG - iwe->cmd = 0x%X, iwe->len = %d\n", iwe->cmd, iwe->len);
#endif

    /* Check invalid events */
    if(iwe->len <= IW_EV_LCP_PK_LEN)
        return(-1);

    /* Get the type and length of that event */
    if(iwe->cmd <= SIOCIWLAST) {
        cmd_index = iwe->cmd - SIOCIWFIRST;
        if(cmd_index < standard_ioctl_num)
            descr = &(standard_ioctl_descr[cmd_index]);
    }
    else {
        cmd_index = iwe->cmd - IWEVFIRST;
        if(cmd_index < standard_event_num)
            descr = &(standard_event_descr[cmd_index]);
    }
    if(descr != NULL)
        event_type = descr->header_type;
    /* Unknown events -> event_type=0 => IW_EV_LCP_PK_LEN */
    event_len = event_type_size[event_type];

    /* Check if we know about this event */
    if(event_len <= IW_EV_LCP_PK_LEN) {
        /* Skip to next event */
        stream->current += iwe->len;
        return(2);
    }
    event_len -= IW_EV_LCP_PK_LEN;


    /* --- We now know the length of the event.  Now to process it. --- */

    /* Set pointer on data */
    if(stream->value != NULL)
        pointer = stream->value;            /* Next value in event */
    else
        pointer = stream->current + IW_EV_LCP_PK_LEN;   /* First value in event */

#ifdef DEBUG
    printf("DBG - event_type = %d, event_len = %d, pointer = %p\n",
        event_type, event_len, pointer);
#endif

    /* Copy the rest of the event (at least, fixed part) */
    if((pointer + event_len) > stream->end) {
        /* Go to next event */
        stream->current += iwe->len;
        return(-2);
    }
    /* Fixup for WE-19 and later : pointer no longer in the stream */
    /* Beware of alignement. Dest has local alignement, not packed */
    if(event_type == IW_HEADER_TYPE_POINT)
        memcpy((char *) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF,
                pointer, event_len);
    else
        memcpy((char *) iwe + IW_EV_LCP_LEN, pointer, event_len);

    /* Skip event in the stream */
    pointer += event_len;

    /* Special processing for iw_point events */
    if(event_type == IW_HEADER_TYPE_POINT) {
        /* Check the length of the payload */
        unsigned int  extra_len = iwe->len - (event_len + IW_EV_LCP_PK_LEN);
        if(extra_len > 0) {
            /* Set pointer on variable part (warning : non aligned) */
            iwe->u.data.pointer = pointer;

            /* Check that we have a descriptor for the command */
            if(descr == NULL)
                /* Can't check payload -> unsafe... */
                iwe->u.data.pointer = NULL; /* Discard paylod */
            else {
                /* Those checks are actually pretty hard to trigger,
                 * because of the checks done in the kernel... */

                unsigned int  token_len = iwe->u.data.length * descr->token_size;

                /* Ugly fixup for alignement issues.
                 * If the kernel is 64 bits and userspace 32 bits,
                 * we have an extra 4+4 bytes.
                 * Fixing that in the kernel would break 64 bits userspace. */
                if((token_len != extra_len) && (extra_len >= 4)) {
                    __u16     alt_dlen = *((__u16 *) pointer);
                    unsigned int  alt_token_len = alt_dlen * descr->token_size;
                    if((alt_token_len + 8) == extra_len) {
#ifdef DEBUG
                        printf("DBG - alt_token_len = %d\n", alt_token_len);
#endif
                        /* Ok, let's redo everything */
                        pointer -= event_len;
                        pointer += 4;
                        /* Dest has local alignement, not packed */
                        memcpy((char *) iwe + IW_EV_LCP_LEN + IW_EV_POINT_OFF,
                                pointer, event_len);
                        pointer += event_len + 4;
                        iwe->u.data.pointer = pointer;
                        token_len = alt_token_len;
                    }
                }

                /* Discard bogus events which advertise more tokens than
                 * what they carry... */
                if(token_len > extra_len)
                    iwe->u.data.pointer = NULL; /* Discard paylod */
                /* Check that the advertised token size is not going to
                 * produce buffer overflow to our caller... */
                if((iwe->u.data.length > descr->max_tokens)
                    && !(descr->flags & IW_DESCR_FLAG_NOMAX))
                    iwe->u.data.pointer = NULL; /* Discard paylod */
                /* Same for underflows... */
                if(iwe->u.data.length < descr->min_tokens)
                    iwe->u.data.pointer = NULL; /* Discard paylod */
#ifdef DEBUG
                printf("DBG - extra_len = %d, token_len = %d, token = %d, max = %d, min = %d\n",
                            extra_len, token_len, iwe->u.data.length, descr->max_tokens, descr->min_tokens);
#endif
            }
        }
        else
            /* No data */
            iwe->u.data.pointer = NULL;

        /* Go to next event */
        stream->current += iwe->len;
    }
    else {
        /* Ugly fixup for alignement issues.
         * If the kernel is 64 bits and userspace 32 bits,
         * we have an extra 4 bytes.
         * Fixing that in the kernel would break 64 bits userspace. */
        if((stream->value == NULL)
            && ((((iwe->len - IW_EV_LCP_PK_LEN) % event_len) == 4)
            || ((iwe->len == 12) && ((event_type == IW_HEADER_TYPE_UINT) ||
                        (event_type == IW_HEADER_TYPE_QUAL))) )) {
#ifdef DEBUG
            printf("DBG - alt iwe->len = %d\n", iwe->len - 4);
#endif
            pointer -= event_len;
            pointer += 4;
            /* Beware of alignement. Dest has local alignement, not packed */
            memcpy((char *) iwe + IW_EV_LCP_LEN, pointer, event_len);
            pointer += event_len;
        }

        /* Is there more value in the event ? */
        if((pointer + event_len) <= (stream->current + iwe->len))
            /* Go to next value */
            stream->value = pointer;
        else {
            /* Go to next event */
            stream->value = NULL;
            stream->current += iwe->len;
        }
    }
    return(1);
}


void
iw_init_event_stream(struct stream_descr *  stream, /* Stream of events */
             char *         data,
             int            len)
{
  /* Cleanup */
  memset((char *) stream, '\0', sizeof(struct stream_descr));

  /* Set things up */
  stream->current = data;
  stream->end = data + len;
}


int iw_get_range_info(int skfd, const char *ifname, iwrange *range) {
    struct iwreq        wrq;
    char                buffer[sizeof(iwrange) * 2]; /* Large enough */

    /* Cleanup */
    bzero(buffer, sizeof(buffer));

    wrq.u.data.pointer = (caddr_t) buffer;
    wrq.u.data.length  = sizeof(buffer);
    wrq.u.data.flags   = 0;
    if(iw_get_ext(skfd, ifname, SIOCGIWRANGE, &wrq) < 0)
        return(-1);

    /* This is our native format, that's easy... */
    /* Copy stuff at the right place, ignore extra */
    memcpy((char *) range, buffer, sizeof(iwrange));

  return(0);
}


/***************************** SCANNING *****************************/



/* --- TAKEN FROM BIONIC ---
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
    char *d = dst;
    const char *s = src;
    size_t n = siz;

    /* Copy as many bytes as will fit */
    if (n != 0) {
        while (--n != 0) {
            if ((*d++ = *s++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src */
    if (n == 0) {
        if (siz != 0)
            *d = '\0';      /* NUL-terminate dst */
        while (*s++)
            ;
    }

    return(s - src - 1);    /* count does not include NUL */
}



static uint16_t
read_le16(uint8_t **data, int32_t *length)
{
	uint16_t value;
	memcpy(&value, *data, sizeof(value));
	*data += 2;
	*length -= 2;
	return value;
}

static uint32_t
read_le32(uint8_t** data, int32_t *length)
{
	uint32_t value;
	memcpy(&value, *data, sizeof(value));
	*data += 4;
	*length -= 4;
	return value;
}

static uint32_t
from_rsn_cipher(uint32_t cipher)
{
	if ((cipher & 0xffffff) != RSN_OUI)
		return ENC_AES;

	switch (cipher >> 24) {
		case RSN_CSE_NULL:
			return ENC_NONE;
		case RSN_CSE_WEP40:
			return ENC_WEP;
		case RSN_CSE_WEP104:
			return ENC_WEP;
		case RSN_CSE_TKIP:
			return ENC_TKIP;
		default:
		case RSN_CSE_CCMP:
			return ENC_AES;
		case RSN_CSE_WRAP:
			return ENC_HUH;
	}
}

static uint32_t
from_rsn_key_mode(uint32_t mode)
{
	if ((mode & 0xffffff) != RSN_OUI)
		return AUTH_DUNNO;

	switch (mode >> 24) {
		default:
		case RSN_ASE_8021X_UNSPEC:
			return AUTH_DUNNO;
		case RSN_ASE_8021X_PSK:
			return AUTH_WPAPSK;
		// the following are currently not defined in net80211
		case 3:
			return AUTH_DUNNO;
		case 4:
			return AUTH_WPAPSK;
		case 5:
			return AUTH_DUNNO;
		case 6:
			return AUTH_WPA2PSK;
	}
}


/* Taken from NetworkDevice.cpp from Haiku */
static void
parse_ie_rsn_wpa(struct ap_description *ap, uint8_t **data, int32_t *length)
{
	if (*length >= 4) {
		// parse group cipher
		ap->encryption = from_rsn_cipher(read_le32(data, length));
	} else if (*length > 0)
		return;

	if (*length >= 2) {
		// parse unicast cipher
		uint16_t count = read_le16(data, length);
		uint16_t i;
		ap->encryption = 0;

		for (i = 0; i < count; i++) {
			if (*length < 4)
				return;
			ap->encryption = from_rsn_cipher(read_le32(data, length));
		}
	} else if (*length > 0)
		return;

	if (*length >= 2) {
		// parse key management mode
		uint16_t count = read_le16(data, length);
		uint16_t i;
		ap->auth = 0;

		for (i = 0; i < count; i++) {
			if (*length < 4)
				return;
			ap->auth |= from_rsn_key_mode(read_le32(data, length));
		}
	} else if (*length > 0)
		return;

	// TODO: capabilities, and PMKID following in case of RSN
}


static void
parse_ie_rsn(struct ap_description *ap, struct ie_data* ie)
{
	ap->auth = AUTH_WPA2PSK;
	ap->encryption = ENC_AES;

	int32_t length = ie->length;
	if (length < 2)
		return;

	uint8_t* data = ie->data;

	uint16_t version = read_le16(&data, &length);
	if (version != RSN_VERSION)
		return;

	parse_ie_rsn_wpa(ap, &data, &length);
}

static uint8_t
parse_ie_wpa(struct ap_description *ap, struct ie_data* ie)
{
	int32_t length = ie->length;
	if (length < 6)
		return FALSE;

	uint8_t* data = ie->data;

	uint32_t oui = read_le32(&data, &length);
	if (oui != ((WPA_OUI_TYPE << 24) | WPA_OUI))
		return FALSE;

	uint16_t version = read_le16(&data, &length);
	if (version != WPA_VERSION)
		return FALSE;

	ap->auth = AUTH_WPAPSK;
	ap->encryption = ENC_TKIP;

	parse_ie_rsn_wpa(ap, &data, &length);
	return TRUE;
}




/* Taken from NetworkDevice.cpp from Haiku */
static int
parse_ie(struct ap_description *ap, uint8_t *_ie, int32_t ieLength) {
	struct ie_data* ie = (struct ie_data*)_ie;
	int hadRSN = FALSE;
	int hadWPA = FALSE;

	while (ieLength > 1) {
		switch (ie->type) {
			case IEEE80211_ELEMID_SSID:
				strlcpy(ap->ssid, (char*)ie->data,
					min_c(ie->length + 1, (int)sizeof(ap->ssid)));
				break;
			case IEEE80211_ELEMID_RSN:
				parse_ie_rsn(ap, ie);
				hadRSN = TRUE;
				break;
			case IEEE80211_ELEMID_VENDOR:
				if (!hadRSN && parse_ie_wpa(ap, ie))
					hadWPA = TRUE;
				break;
		}

		ieLength -= 2 + ie->length;
		ie = (struct ie_data*)((uint8_t*)ie + 2 + ie->length);
	}

	if (hadRSN || hadWPA) {
		// Determine authentication mode

/*
		if ((network.key_mode & (B_KEY_MODE_IEEE802_1X_SHA256
				| B_KEY_MODE_PSK_SHA256)) != 0) {
			network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA2;
		} else if ((network.key_mode & (B_KEY_MODE_IEEE802_1X
				| B_KEY_MODE_PSK | B_KEY_MODE_FT_IEEE802_1X
				| B_KEY_MODE_FT_PSK)) != 0) {
			if (!hadRSN)
				network.authentication_mode = B_NETWORK_AUTHENTICATION_WPA;
		} else if ((network.key_mode & B_KEY_MODE_NONE) != 0) {
			if ((network.cipher & (B_NETWORK_CIPHER_WEP_40
					| B_NETWORK_CIPHER_WEP_104)) != 0)
				network.authentication_mode = B_NETWORK_AUTHENTICATION_WEP;
			else
				network.authentication_mode = B_NETWORK_AUTHENTICATION_NONE;
		}
*/
	}
	return 0;
}

static void print_ap(struct ap_description *ap) {
    found_ap_count++;
    found_aps = realloc(found_aps, (found_ap_count+1) * sizeof(struct ap_description));
    memcpy(&found_aps[found_ap_count-1], ap, sizeof(*ap));
    found_aps[found_ap_count].populated = 0;
    ap->printed = 1;
}

static int populate_and_print_ap(struct iw_event *event,
                                 struct ap_description *ap,
                                 struct iw_range *range) {
    int printed = 0;
    switch(event->cmd) {
        case SIOCGIWAP: {
            struct ether_addr *ea = (struct ether_addr *)&event->u.ap_addr.sa_data;
            // This is a new AP.  Print out the old AP and reset it.
            if(ap->populated) {
                print_ap(ap);
                printed = 1;
            }
            bzero(ap, sizeof(*ap));
            memcpy(ap->hwaddr, &ea->ether_addr_octet, sizeof(ap->hwaddr));
            return 1;
            break;
        }

        // Link quality
        case IWEVQUAL:
            ap->linkquality    = event->u.qual.qual;
            ap->signalstrength = event->u.qual.level;
            ap->noiselevel     = event->u.qual.noise;
            break;

        // SSID name
        case SIOCGIWESSID:
            if((event->u.essid.pointer) && (event->u.essid.length))
                memcpy(ap->ssid, event->u.essid.pointer, event->u.essid.length);
            break;

        // Channel/frequency information
        case SIOCGIWFREQ: {
            double freq = event->u.freq.m * pow(10, event->u.freq.e);
            int k;

            // If we don't have range information, quit.
            if(!range) {
                ap->channel = freq;
                break;
            }

            // Convert frequency to channel number.
            for(k = 0; k < range->num_frequency; k++) {
                double ref_freq = range->freq[k].m * pow(10, range->freq[k].e);
                if(freq == ref_freq) {
                    ap->channel = range->freq[k].i;
                    break;
                }
            }
            break;
        }

        case SIOCGIWENCODE: {
            unsigned char key[IW_ENCODING_TOKEN_MAX];

            /* If we have a key, copy it over. */
            if(event->u.data.pointer)
                memcpy(key, event->u.data.pointer, event->u.data.length);
            else
                event->u.data.flags |= IW_ENCODE_NOKEY;

            /* If the 'encode-disabled' bit is set, then it's an open network */
            if(event->u.data.flags & IW_ENCODE_DISABLED) {
                ap->auth       = AUTH_OPEN;
                ap->encryption = ENC_NONE;
            }

            /* If we hit an encryption type for the first time, assume
             * we're WEP.  If we hit a GenIE block later we'll expand upon
             * that and replace the type when we get there.
             */
            else if(ap->auth == AUTH_OPEN && ap->encryption == ENC_NONE) {
                ap->auth       = AUTH_WEPAUTO;
                ap->encryption = ENC_WEP;
            }

            break;
        }

        case SIOCGIWRATE:
//            fprintf(stderr, "Got bit rates\n");
            break;

        case IWEVGENIE: {
            uint8_t *ie_data = (uint8_t *)event->u.data.pointer;
            int      length  = event->u.data.length;
            parse_ie(ap, ie_data, length);

            break;
        }

        case IWEVCUSTOM: {
            /* -- Mostly uninteresting to our purposes
            char custom[IW_CUSTOM_MAX+1];
            int pos;
            if((event->u.data.pointer) && (event->u.data.length))
                memcpy(custom, event->u.data.pointer, event->u.data.length);
            custom[event->u.data.length] = '\0';

            fprintf(stderr, "Got custom %d bytes: ", event->u.data.length);
            for(pos=0; pos<event->u.data.length; pos++)
                fprintf(stderr, "%02x", custom[pos]);
            fprintf(stderr, " (%s)\n", custom);
            */


            break;
        }

        case SIOCGIWMODE:
            ap->mode = event->u.mode;
            break;

        default:
            fprintf(stderr, "Unrecognized command: %d\n", event->cmd);
            break;
    }
    ap->populated = 1;
    return printed;
}





/*------------------------------------------------------------------*/
/*
 * Perform a scanning on one device
 */
static int
print_scanning_info(int skfd, char *ifname, char *ssid, int immediate) {
    struct iwreq          wrq;
    struct iw_scan_req    scanopt;                /* Options for 'set' */
    int                   scanflags = 0;          /* Flags for scan */
    unsigned char *       buffer = NULL;          /* Results */
    int                   buflen = IW_SCAN_MAX_DATA; /* Min for compat WE<17 */
    struct iw_range       range;
    int                   has_range;
    struct timeval        tv;                             /* Select timeout */
    int                   timeout = 15000000;             /* 15s */
    int                   ap_count = 0;
    struct ap_description ap_description;

    bzero(&ap_description, sizeof(ap_description));

    /* Debugging stuff */
    if((IW_EV_LCP_PK2_LEN != IW_EV_LCP_PK_LEN) || (IW_EV_POINT_PK2_LEN != IW_EV_POINT_PK_LEN)) {
      fprintf(stderr, "*** Please report to jt@hpl.hp.com your platform details\n");
      fprintf(stderr, "*** and the following line :\n");
      fprintf(stderr, "*** IW_EV_LCP_PK2_LEN = %zu ; IW_EV_POINT_PK2_LEN = %zu\n\n",
              IW_EV_LCP_PK2_LEN, IW_EV_POINT_PK2_LEN);
    }

    /* Get range stuff */
    has_range = (iw_get_range_info(skfd, ifname, &range) >= 0);

    /* Check if the interface could support scanning. */
    if((!has_range)) {
        fprintf(stderr, "%-8.16s  Interface doesn't support scanning.\n\n",
                ifname);
        return(-1);
    }

    /* Init timeout value -> 250ms between set and first get */
    tv.tv_sec  = 0;
    tv.tv_usec = 250000;

    /* Clean up set args */
    memset(&scanopt, 0, sizeof(scanopt));



    if(immediate) {
        /* Hack */
        scanflags |= IW_SCAN_HACK;
    }



    /* Check if we have scan options */
    if(scanflags) {
        wrq.u.data.pointer = (caddr_t) &scanopt;
        wrq.u.data.length  = sizeof(scanopt);
        wrq.u.data.flags   = scanflags;
    }
    else {
        wrq.u.data.pointer = NULL;
        wrq.u.data.flags   = 0;
        wrq.u.data.length  = 0;
    }

    /* If only 'last' was specified on command line, don't trigger a scan */
    if(scanflags == IW_SCAN_HACK) {
        /* Skip waiting */
        tv.tv_usec = 0;
    }
    else {
        /* Initiate Scanning */
        if(iw_set_ext(skfd, ifname, SIOCSIWSCAN, &wrq) < 0) {
            if((errno != EPERM) || (scanflags != 0)) {
                fprintf(stderr, "%-8.16s  Interface doesn't support scanning : %s\n\n",
                        ifname, strerror(errno));
                return(-1);
            }
            /* If we don't have the permission to initiate the scan, we may
             * still have permission to read left-over results.
             * But, don't wait !!! */
            tv.tv_usec = 0;
        }
    }
    timeout -= tv.tv_usec;

    /* Forever */
    while(1) {
        fd_set            rfds;           /* File descriptors for select */
        int               last_fd;        /* Last fd */
        int               ret;

        /* Guess what ? We must re-generate rfds each time */
        FD_ZERO(&rfds);
        last_fd = -1;

        /* In here, add the rtnetlink fd in the list */

        /* Wait until something happens */
        ret = select(last_fd + 1, &rfds, NULL, NULL, &tv);

        /* Check if there was an error */
        if(ret < 0) {
            if(errno == EAGAIN || errno == EINTR)
                continue;
            fprintf(stderr, "Unhandled signal - exiting...\n");
            return(-1);
        }

        /* Check if there was a timeout */
        if(ret == 0) {
            unsigned char *       newbuf;

            realloc:
            /* (Re)allocate the buffer - realloc(NULL, len) == malloc(len) */
            newbuf = realloc(buffer, buflen);
            if(newbuf == NULL) {
                if(buffer)
                    free(buffer);
                fprintf(stderr, "%s: Allocation failed\n", __FUNCTION__);
                return(-1);
            }
            buffer = newbuf;

            /* Try to read the results */
            wrq.u.data.pointer = buffer;
            wrq.u.data.flags = 0;
            wrq.u.data.length = buflen;
            if(iw_get_ext(skfd, ifname, SIOCGIWSCAN, &wrq) < 0) {
                /* Check if buffer was too small (WE-17 only) */
                if((errno == E2BIG) && (range.we_version_compiled > 16)) {
                    /* Some driver may return very large scan results, either
                    * because there are many cells, or because they have many
                    * large elements in cells (like IWEVCUSTOM). Most will
                    * only need the regular sized buffer. We now use a dynamic
                    * allocation of the buffer to satisfy everybody. Of course,
                    * as we don't know in advance the size of the array, we try
                    * various increasing sizes. Jean II */

                    /* Check if the driver gave us any hints. */
                    if(wrq.u.data.length > buflen)
                        buflen = wrq.u.data.length;
                    else
                        buflen *= 2;

                    /* Try again */
                    goto realloc;
                }

                /* Check if results not available yet */
                if(errno == EAGAIN) {
                    /* Restart timer for only 100ms*/
                    tv.tv_sec = 0;
                    tv.tv_usec = 100000;
                    timeout -= tv.tv_usec;
                    if(timeout > 0)
                        continue;   /* Try again later */
                }

                /* Bad error */
                free(buffer);
                fprintf(stderr, "%-8.16s  Failed to read scan data : %s\n\n",
                        ifname, strerror(errno));
                return(-2);
            }
            else
                /* We have the results, go to process them */
                break;
        }

        /* In here, check if event and event type
        * if scan event, read results. All errors bad & no reset timeout */
    }

    if(wrq.u.data.length) {
        struct iw_event           iwe;
        struct stream_descr       stream;
        int                       ret;
        
        iw_init_event_stream(&stream, (char *) buffer, wrq.u.data.length);

        // Reset the description.
        bzero(&ap_description, sizeof(ap_description));
        do {
            /* Extract an event and print it */
            if((ret = iw_extract_event_stream(&stream, &iwe)) > 0)
                ap_count += populate_and_print_ap(&iwe, &ap_description, &range);
        }
        while(ret > 0);
        ap_count += populate_and_print_ap(&iwe, &ap_description, &range);
    }

    if(!ap_description.printed)
        print_ap(&ap_description);

    free(buffer);
    return(ap_count);
}





/******************************* MAIN ********************************/

struct ap_description *ap_scan(char *dev) {
    int   skfd;                         /* generic raw socket desc.     */
    int   immediate = 0;                /* Whether to wait for a scan   */
    int   printed = 0;
    int   print_try;

    if (!dev)
        dev = DEFAULT_DEVICE;

    free(found_aps);
    found_aps = NULL;
    found_ap_count = 0;

    /* Create a channel to the NET kernel. */
    if((skfd = iw_sockets_open()) < 0) {
        perror("socket");
        return NULL;
    }

    /* do the actual work */
    for(print_try=0; print_try<2 && !printed; print_try++)
        printed=print_scanning_info(skfd, dev, NULL, immediate);

    /* Close the socket. */
    iw_sockets_close(skfd);

#if 0
                printf("  <ap"
                            " mode=\"%s\""
                            " encryption=\"%s\""
                            " channel=\"%d\""
                            " hwaddr=\"%02x:%02x:%02x:%02x:%02x:%02x\""
                            " auth=\"%s\""
                            " linkquality=\"%d\""
                            " signalstrength=\"%2.1f\""
                            " noiselevel=\"%d\""
                            " ssid=\"%s\""
                            " wps=\"%s\""
                            " />\n",
   /* mode */               iw_operation_mode[ap->mode],
   /* encryption */         chumby_encryptions[ap->encryption],
   /* channel */            ap->channel,
   /* hwaddr */             ap->hwaddr[0], ap->hwaddr[1], ap->hwaddr[2],
                            ap->hwaddr[3], ap->hwaddr[4], ap->hwaddr[5],
   /* auth */               chumby_auths[ap->auth],
   /* linkquality */        ap->linkquality,
                            ap->signalstrength/-10.,
                            ap->noiselevel,
                            ap->ssid,
                            wps_string(ap));
#endif

    return found_aps;
}
#else
#include <string.h>
#include <unistd.h>

static struct ap_description aps[4];

struct ap_description *ap_scan(char *dev) {
    sleep(1);
    strncpy(aps[0].ssid, "Test AP 1 (WPA)", sizeof(aps[0].ssid));
    strncpy(aps[1].ssid, "Test AP 2 (WEP)", sizeof(aps[1].ssid));
    strncpy(aps[2].ssid, "Test AP 3 (Open)", sizeof(aps[2].ssid));
    aps[0].populated = 1;
    aps[1].populated = 1;
    aps[2].populated = 1;
    aps[3].populated = 0;

    aps[0].auth = AUTH_WPAPSK;
    aps[1].auth = AUTH_WEPAUTO;
    aps[3].auth = AUTH_OPEN;

    return aps;
}

#endif
