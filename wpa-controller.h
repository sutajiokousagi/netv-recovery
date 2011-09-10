#ifndef __WPA_CONTROLLER_H__
#define __WPA_CONTROLLER_H__
struct wpa_process;
struct wpa_process *start_wpa(char *ssid, char *key);
int poll_wpa(struct wpa_process *process, int blocking);
int stop_wpa(struct wpa_process *process);
#endif /* __WPA_CONTROLLER_H__ */
