#ifndef __AP_SCAN_H__
#define __AP_SCAN_H__


enum chumby_encryption_type {
    ENC_NONE,
    ENC_WEP,
    ENC_AES,
    ENC_TKIP,
};

enum chumby_auth_type {
    AUTH_OPEN,
    AUTH_WEPAUTO,
    AUTH_WPA2PSK,
    AUTH_WPAPSK,
    AUTH_WPA2EAP,
    AUTH_WPAEAP,
};


struct ap_description {
    int                         populated;
    enum chumby_auth_type       auth;
    int                         mode;
    int                         channel;
    int                         signalstrength;
    int                         linkquality;
    int                         noiselevel;
    enum chumby_encryption_type encryption;
    char                        ssid[33];
    char                        hwaddr[6];
    int                         wps;
    int                         printed;
};

struct ap_description *ap_scan(void);
#endif /* __AP_SCAN_H__ */
