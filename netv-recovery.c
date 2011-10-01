#include <stdio.h>
#include <SDL/SDL.h>
#include <SDL_ttf.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <zlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/mount.h>

#define RECOVERY_VERSION 0x010002
char current_debug_message[1024];
static struct textbox *debug_textbox;

#ifdef linux
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <linux/kd.h>
# define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)
# define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)
#else
# define init_module(mod, len, opts) 0
# define delete_module(mod, flags) 0
#endif

#include "sdl-keyboard.h"
#include "sdl-picker.h"
#include "sdl-textbox.h"
#include "sdl-progress.h"
#include "wpa-controller.h"
#include "ap-scan.h"
#include "ufdisk.h"
#include "myifup.h"
#include "dhcpc.h"
#include "udev.h"
#include "wget.h"
#include "gunzip.h"
#include "config-area.h"

#define ICON_W 64
#define ICON_H 64
#define ICON_PADDING 8

#define SELECT_SSID 1
#define TYPE_SSID 2
#define SELECT_ENCRYPTION 3
#define TYPE_KEY 4
#define START_CONNECTING 5
#define CONNECTED 6
#define CONNECTION_ERROR 7
#define UNSUPPORTED_ENCRYPTION 8
#define DOWNLOADING 9
#define UNRECOVERABLE 10
#define DONE 11

#define ENC_OPEN 0
#define ENC_WPA 1

int errfd;
FILE *errfil;

//#define IMAGE_URL "http://buildbot.chumby.com.sg/build/silvermoon-netv/LATEST/disk-image.gz"
#define IMAGE_URL "http://175.41.134.235/build/silvermoon-netv/LATEST/disk-image.gz"
#define OTHER_NETWORK_STRING "[Other Network]"
struct recovery_data;

#define MAKEDRAW(x) ((void (*)(void *, void *))x)
#define MAKEPRESS(x) ((void (*)(void *, int))x)
#define MAKEFUNC(x) ((void (*)(struct recovery_data *))x)

#define PERROR(format, arg...)            \
  do { \
    fprintf(stderr, "netv-recovery.c - %s():%d - " format ": %s\n", __func__, __LINE__, ## arg, strerror(errno)); \
    snprintf(current_debug_message, sizeof(current_debug_message), "netv-recovery.c - %s():%d - " format ": %s\n", __func__, __LINE__, ## arg, strerror(errno)); \
  } while(0)
#define ERROR(format, arg...)            \
  do { \
    fprintf(stderr, "netv-recovery.c - %s():%d - " format "\n", __func__, __LINE__, ## arg); \
    snprintf(current_debug_message, sizeof(current_debug_message), "netv-recovery.c - %s():%d - " format "\n", __func__, __LINE__, ## arg); \
  } while(0)
#define NOTE(format, arg...)            \
  do { \
    fprintf(stderr, "netv-recovery.c - %s():%d - " format "\n", __func__, __LINE__, ## arg); \
    snprintf(current_debug_message, sizeof(current_debug_message), "netv-recovery.c - %s():%d - " format "\n", __func__, __LINE__, ## arg); \
  } while(0)

static const char *auth_type_str[] = {
    "OPEN",
    "WEPAUTO",
    "WPA2PSK",
    "WPAPSK",
    "WPA2EAP",
    "WPAEAP",
};


struct scene_element {
    void (*draw)(void *data, void *screen);
    void (*press)(void *data, int key);
    void *data;
};

struct scene {
    struct scene_element elements[128];
    int num_elements;
    int id;
    void (*function)(struct recovery_data *data);
};

struct recovery_data {
    SDL_Surface *screen;
    struct scene *scene;
    struct scene scenes[30];

    char *ssid;
    char *key;
    char *ifname;

    int dhcpc_pid;

    struct ap_description *aps;

    int data_size;
    int last_data_size;

    int encryption_type;
    int should_quit;
};



static int
move_to_scene(struct recovery_data *data, int scene);

static int
redraw_scene(struct recovery_data *data);




static void
set_ssid(struct recovery_data *data, char *str)
{
    if (data->ssid)
        free(data->ssid);
    data->ssid = malloc(strlen(str)+1);
    strcpy(data->ssid, str);
}

static void
set_key(struct recovery_data *data, char *str)
{
    if (data->key)
        free(data->key);
    data->key = malloc(strlen(str)+1);
    strcpy(data->key, str);
}


static int
pressed_key(int key, void *_data)
{
    struct recovery_data *data = _data;

    if (data->scene->id == TYPE_SSID
     || data->scene->id == TYPE_KEY) {
        struct textbox *title_textbox;
        char *str;
        int new_str_len = 1;

        title_textbox = (struct textbox *)data->scene->elements[1].data;

        str = get_text_textbox(title_textbox);
        if (str)
            new_str_len = strlen(str)+1;

        if (key == '\b') {
            if (new_str_len > 1)
                str[new_str_len-2] = '\0';
            set_text_textbox(title_textbox, str);
        }
        else if(key == '\n') {
            if (data->scene->id == TYPE_SSID) {
                set_ssid(data, str);
                move_to_scene(data, SELECT_ENCRYPTION);
            }
            else if(data->scene->id == TYPE_KEY) {
                set_key(data, str);
                move_to_scene(data, START_CONNECTING);
            }
        }
        else if(key == '\t') {
            ;
        }
        else {
            char newstr[new_str_len+1];
            bzero(newstr, sizeof(newstr));
            strcpy(newstr, str);
            newstr[new_str_len-1] = key;
            set_text_textbox(title_textbox, newstr);
        }
    }

    return 0;
}


static int
pick_encryption(char *item, void *_data)
{
    struct recovery_data *data = _data;
    struct picker *picker = (struct picker *)data->scene->elements[0].data;

    if (picker->active_entry == 0) {
        fprintf(stderr, "Using open encryption\n");
        data->encryption_type = ENC_OPEN;
        move_to_scene(data, START_CONNECTING);
    }
    else {
        fprintf(stderr, "Using WPA\n");
        data->encryption_type = ENC_WPA;
        move_to_scene(data, TYPE_KEY);
    }
    return 0;
}


static int
pick_ssid(char *item, void *_data)
{
    struct recovery_data *data = _data;
    struct picker *picker;
    struct ap_description *ap;

    picker = (struct picker *)data->scene->elements[0].data;
    if (data->aps)
        ap = data->aps+picker->active_entry;
    else
        ap = NULL;

    if (!ap || !ap->populated) {
        move_to_scene(data, TYPE_SSID);
    }
    else {
        set_ssid(data, ap->ssid);
        if (ap->auth == AUTH_OPEN) {
            data->encryption_type = ENC_OPEN;
            move_to_scene(data, START_CONNECTING);
        }
        else if(ap->auth == AUTH_WPAPSK || ap->auth == AUTH_WPA2PSK) {
            data->encryption_type = ENC_WPA;
            move_to_scene(data, TYPE_KEY);
        }
        else {
            NOTE("User attempted to pick ap %s with auth type %s", ap->ssid, auth_type_str[ap->auth]);
            move_to_scene(data, UNSUPPORTED_ENCRYPTION);
        }
    }
    return 0;
}


static int
write_file_to_config_area(char *file, char *blk, struct config_area *ca, int fd)
{
    struct block_def *bd;
    int length, offset, block_count;
    int src;

    bd = ca->block_table;
    length = 0;
    offset = 0;
    block_count = 0;
    while(!length && !offset && bd->offset != 0xffffffff && block_count < 64) {
        block_count++;
        if (!strncmp(blk, bd->n.name, 4)) {
            length = bd->length;
            offset = bd->offset;
            NOTE("Found %s at offset %d, %d bytes long", blk, offset, length);
        }
        bd++;
    }

    if (!length || !offset) {
        ERROR("Couldn't find %s in config block!", blk);
        return -1;
    }

    src = open(file, O_RDONLY);
    if (-1 == src) {
        PERROR("Couldn't open %s", file);
        return -1;
    }

    if (-1 == lseek(fd, offset, SEEK_SET)) {
        PERROR("Couldn't seek to offset of %s", blk);
        close(src);
        return -1;
    }

    while(length > 0) {
        int rd, wr;
        char bfr[4096];
        rd = read(src, bfr, length>sizeof(bfr)?sizeof(bfr):length);
        if (rd == -1) {
            PERROR("Unable to read source kernel");
            close(src);
            return -1;
        }
        if (!rd) {
            NOTE("Reached the end of the kernel on disk, with %d bytes left",
                length);
            break;
        }

        wr = write(fd, bfr, rd);
        if (rd != wr) {
            PERROR("Unable to write to %s", blk);
            close(src);
            return -1;
        }

        length -= rd;
    }

    close(src);
    return 0;
}


static int
restore_kernel(struct recovery_data *data)
{
    int fd;
    struct config_area ca;

    mkdir("/mnt", 0777);
    if (-1 == mount("/dev/mmcblk0p2", "/mnt", "ext2", MS_RDONLY, NULL)) {
        PERROR("Couldn't mount filesystem");
        return -1;
    }

    fd = open("/dev/mmcblk0p1", O_RDWR);
    if (-1 == fd) {
        PERROR("Couldn't open partition 1");
        umount("/mnt");
        return -1;
    }

    /* The config block is located at the magic offset of 96 */
    if (-1 == lseek(fd, 96*512, SEEK_SET)) {
        PERROR("Couldn't seek to config block");
        umount("/mnt");
        close(fd);
        return -1;
    }

    /* Read the config block straight off the disk */
    if (read(fd, &ca, sizeof(ca)) != sizeof(ca)) {
        PERROR("Couldn't read config area");
        umount("/mnt");
        close(fd);
        return -1;
    }

    /* Verify the config area's signature */
    if(ca.sig[0] != 'C' || ca.sig[1] != 'f'
    || ca.sig[2] != 'g' || ca.sig[3] != '*') {
        ERROR("Config block doesn't have proper signature "
              " (Wanted Cfg*, got %c%c%c%c)", 
              ca.sig[0], ca.sig[1],
              ca.sig[2], ca.sig[3]);
        umount("/mnt");
        close(fd);
        return -1;
    }

    if (-1 == write_file_to_config_area("/mnt/boot/zImage", "krnA", &ca, fd)) {
        ERROR("Couldn't write kernel");
        close(fd);
        umount("/mnt");
        return -1;
    }
    
    if (-1 == write_file_to_config_area("/mnt/boot/logo-preparing.raw.gz", "logo", &ca, fd)) {
        NOTE("Couldn't write new logo, but error is nonfatal");
    }
    

    close(fd);
    umount("/mnt");
    return 0;
}

static int
download_progress(void *_data, int current)
{
    struct recovery_data *data = _data;
    struct progress *progress = data->scene->elements[2].data;
    static int last_percentage = 0;
    int ds = data->data_size;
    int percentage;

    if (!ds)
        ds = 1;
    if (current < (data->last_data_size + 32768))
        return 0;
    percentage = (current>>8)*100/(ds>>8);

    if (percentage != last_percentage)
        NOTE("Download progress: %d%% (%d/%d)",
            percentage, current, data->data_size);
    last_percentage = percentage;

    set_progress(progress, percentage);
    redraw_scene(data);
    data->last_data_size = current;
    return 0;
}

static int
do_download(struct recovery_data *data)
{
    FILE *in;
    int out;
    int ret;

    redraw_scene(data);

    ret = prepare_partitions();
    if (ret == -6) {
        NOTE("Simulation mode detected");
    }
    else if (ret) {
        ERROR("Unable to prepare disk: %d", ret);
        move_to_scene(data, UNRECOVERABLE);
    }


    if (ret == -6)
        out = open("output.bin", O_WRONLY | O_CREAT, 0777);
    else
        out = open("/dev/mmcblk0p2", O_WRONLY);
    if (-1 == out) {
        PERROR("Unable to open output file for compression");
        move_to_scene(data, UNRECOVERABLE);
        return -1;
    }

    in = start_wget(IMAGE_URL, &data->data_size);
    if (in <= 0) {
        PERROR("Couldn't wget");
        move_to_scene(data, UNRECOVERABLE);
        return -1;
    }
    if (!data->data_size) {
        ERROR("Data size was reported as 0 bytes!");
        data->data_size = 1;
        move_to_scene(data, UNRECOVERABLE);
        return -1;
    }
    else
        NOTE("Doing download.  Data size is %d bytes", data->data_size);

    ret = unpack_gz_stream(in, out, download_progress, data);
    close(out);
    fclose(in);

    /* Attempt to restore the kernel */
    NOTE("Attempting to restore kernel...");
    if (restore_kernel(data)) {
        ERROR("Kernel restoration failed");
        move_to_scene(data, UNRECOVERABLE);
        return -1;
    }


    NOTE("Finished restoration");
    move_to_scene(data, DONE);
    return 0;
}

static int
wait_a_sec(struct recovery_data *data)
{
    redraw_scene(data);
    sleep(1);
    move_to_scene(data, DOWNLOADING);
    return 0;
}

static int
establish_connection(struct recovery_data *data)
{
    struct wpa_process *process;
    int ret;

    redraw_scene(data);

    process = start_wpa(data->ssid, data->encryption_type==ENC_WPA ? data->key : NULL, data->ifname);
    if (!process) {
        fprintf(stderr, "Couldn't start WPA\n");
        move_to_scene(data, SELECT_ENCRYPTION);
        return -1;
    }

    do {
        ret = poll_wpa(process, 0);
    } while (!ret);


    if (ret < 0) {
        fprintf(stderr, "Connection error: %d\n", ret);
        move_to_scene(data, CONNECTION_ERROR);
        stop_wpa(process);
    }
    else {
        NOTE("Obtaining IP for interface %s", data->ifname);
	data->dhcpc_pid = udhcpc_main(data->ifname);
        if (data->dhcpc_pid < 0) {
            ERROR("Connection error: %d", data->dhcpc_pid);
            move_to_scene(data, CONNECTION_ERROR);
        }
        NOTE("Connected!");
        move_to_scene(data, CONNECTED);
    }

    redraw_scene(data);

    return 0;
}

static int my_init_module(char *path) {
    int fd;
    struct stat st;
    int ret;

    NOTE("Loading module %s", path);
    fd = open(path, O_RDONLY);
    if (fd == -1) {
        PERROR("Unable to open module");
        return -1;
    }
    fstat(fd, &st);

    char dat[st.st_size];
    if (read(fd, dat, sizeof(dat)) != sizeof(dat)) {
        PERROR("Couldn't read");
        close(fd);
        return -2;
    }
    close(fd);

    ret = init_module(dat, sizeof(dat), "");
    if (ret)
        PERROR("Unable to load module");
    return ret;
}


static int
run_ap_scan(struct recovery_data *data)
{
    struct picker *picker = data->scene->elements[0].data;
    struct textbox *textbox = data->scene->elements[1].data;
    int i;

    clear_picker(picker);
    set_label_textbox(textbox, "Scanning for networks...");
    redraw_scene(data);
    if (!my_ifup("wlan0"))
        data->ifname = "wlan0";
    else if (!my_ifup("wlan1"))
        data->ifname = "wlan1";
    else if (!my_ifup("wlan2"))
        data->ifname = "wlan2";
    else if (!my_ifup("wlan3"))
        data->ifname = "wlan3";
    NOTE("Found interface %s", data->ifname);
    data->aps = ap_scan(data->ifname);

    clear_picker(picker);
    for (i=0; data->aps && data->aps[i].populated; i++) {
        NOTE("Found AP[%d]: %s (%s)", i, data->aps[i].ssid, auth_type_str[data->aps[i].auth]);
        add_item_to_picker(picker, data->aps[i].ssid);
    }
    add_item_to_picker(picker, OTHER_NETWORK_STRING);

    set_label_textbox(textbox, "Select network:");
    return 0;
}

void
do_reboot(struct textbox *txt, int key)
{
#ifdef linux
    reboot(RB_AUTOBOOT);
#endif
}

void
try_again(struct textbox *txt, int key)
{
    move_to_scene(txt->data, SELECT_SSID);
}

static void
sig_handle(int sig)
{
    NOTE("Got signal %d", sig);
    if (sig == SIGTERM) {
        SDL_Quit();
        exit(1);
    }
    if (sig == SIGSEGV || sig == SIGILL || sig == SIGTRAP || sig == SIGFPE) {
        NOTE("That was a very bad signal.  Aborting.");
        SDL_Quit();
        exit(1);
    }
}



static int
process_key(struct recovery_data *data, int sym)
{
    if (data->scene->elements[0].press)
        data->scene->elements[0].press(data->scene->elements[0].data, sym);
    return 0;
}

static int
redraw_scene(struct recovery_data *data)
{
    int i;

    SDL_FillRect(data->screen, NULL, 0);
    for (i=0; i<data->scene->num_elements; i++)
        data->scene->elements[i].draw(data->scene->elements[i].data, data->screen);
    set_label_textbox(debug_textbox, current_debug_message);
    redraw_textbox(debug_textbox, data->screen);
    SDL_Flip(data->screen);

    return 0;
}

static int
move_to_scene(struct recovery_data *data, int scene)
{
    int i;
    for (i=0;
         i<sizeof(data->scenes)/sizeof(data->scenes[0]);
         i++) {
        if (data->scenes[i].id == scene) {
            data->scene = &data->scenes[i];

            if (data->scene->function)
                data->scene->function(data);
            return 1;
        }
    }
    fprintf(stderr, "Couldn't find scene %d!\n", scene);
    return 0;
}

static int
setup_scenes(struct recovery_data *data)
{
    struct keyboard *kbd;
    struct picker *picker;
    struct textbox *textbox;
    struct textbox *textbox2;
    struct progress *progress;

    /* Select SSID */
    NOTE("Creating scene 1");
    picker = create_picker();
    picker->x = 140;
    picker->y = 120;
    picker->w = 1000;
    picker->h = 500;
    picker->data = data;
    picker->pick_item = pick_ssid;

    debug_textbox = create_textbox();
    debug_textbox->x = 140;
    debug_textbox->y = 650;
    debug_textbox->w = 1000;
    debug_textbox->h = 128;
    debug_textbox->data = data;

    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Scanning for networks...");

    data->scenes[0].id = SELECT_SSID;
    data->scenes[0].elements[0].data = picker;
    data->scenes[0].elements[0].draw = MAKEDRAW(redraw_picker);
    data->scenes[0].elements[0].press = MAKEPRESS(press_picker);
    data->scenes[0].elements[1].data = textbox;
    data->scenes[0].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[0].num_elements = 2;
    data->scenes[0].function = MAKEFUNC(run_ap_scan);



    NOTE("Creating scene 2");
    kbd = create_keyboard(KEYS, SHIFTS);
    kbd->x = 120;
    kbd->y = 270;
    kbd->data = data;
    kbd->send_key = pressed_key;

    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 180;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Network name: ");
    set_text_textbox(textbox, "");

    data->scenes[1].id = TYPE_SSID;
    data->scenes[1].elements[0].data = kbd;
    data->scenes[1].elements[0].draw = MAKEDRAW(redraw_keyboard);
    data->scenes[1].elements[0].press = MAKEPRESS(press_keyboard);
    data->scenes[1].elements[1].data = textbox;
    data->scenes[1].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[1].num_elements = 2;



    NOTE("Creating scene 3");
    picker = create_picker();
    picker->x = 140;
    picker->y = 120;
    picker->w = 1000;
    picker->h = 500;
    picker->data = data;
    picker->pick_item = pick_encryption;

    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Select encryption type");

    add_item_to_picker(picker, "Open");
    add_item_to_picker(picker, "WPA");

    data->scenes[2].id = SELECT_ENCRYPTION;
    data->scenes[2].elements[0].data = picker;
    data->scenes[2].elements[0].draw = MAKEDRAW(redraw_picker);
    data->scenes[2].elements[0].press = MAKEPRESS(press_picker);
    data->scenes[2].elements[1].data = textbox;
    data->scenes[2].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[2].num_elements = 2;



    NOTE("Creating scene 4");
    kbd = create_keyboard(KEYS, SHIFTS);
    kbd->x = 120;
    kbd->y = 270;
    kbd->data = data;
    kbd->send_key = pressed_key;

    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Password: ");

    data->scenes[3].id = TYPE_KEY;
    data->scenes[3].elements[0].data = kbd;
    data->scenes[3].elements[0].draw = MAKEDRAW(redraw_keyboard);
    data->scenes[3].elements[0].press = MAKEPRESS(press_keyboard);
    data->scenes[3].elements[1].data = textbox;
    data->scenes[3].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[3].num_elements = 2;




    NOTE("Creating scene 5");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Connecting...");

    data->scenes[4].id = START_CONNECTING;
    data->scenes[4].elements[0].data = textbox;
    data->scenes[4].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[4].num_elements = 1;
    data->scenes[4].function = MAKEFUNC(establish_connection);



    NOTE("Creating scene 6");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Connected");

    data->scenes[5].id = CONNECTED;
    data->scenes[5].elements[0].data = textbox;
    data->scenes[5].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[5].function = MAKEFUNC(wait_a_sec);
    data->scenes[5].num_elements = 1;



    NOTE("Creating scene 7");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Error");


    textbox2 = create_textbox();
    textbox2->x = 140;
    textbox2->y = 180;
    textbox2->w = 1000;
    textbox2->h = 128;
    textbox2->data = data;
    set_label_textbox(textbox2, "Press any key to try again");

    data->scenes[6].id = CONNECTION_ERROR;
    data->scenes[6].elements[0].data = textbox;
    data->scenes[6].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[6].elements[0].press= MAKEPRESS(try_again);
    data->scenes[6].elements[1].data = textbox2;
    data->scenes[6].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[6].num_elements = 2;



    NOTE("Creating scene 8");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Unsupported network");


    textbox2 = create_textbox();
    textbox2->x = 140;
    textbox2->y = 180;
    textbox2->w = 1000;
    textbox2->h = 128;
    textbox2->data = data;
    set_label_textbox(textbox2, "Only unencrypted and WPA-encrypted networks are supported");

    data->scenes[7].id = UNSUPPORTED_ENCRYPTION;
    data->scenes[7].elements[0].data = textbox;
    data->scenes[7].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[7].elements[0].press= MAKEPRESS(try_again);
    data->scenes[7].elements[1].data = textbox2;
    data->scenes[7].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[7].num_elements = 2;



    NOTE("Creating scene 9");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Downloading...");


    textbox2 = create_textbox();
    textbox2->x = 140;
    textbox2->y = 180;
    textbox2->w = 1000;
    textbox2->h = 128;
    textbox2->data = data;
    set_label_textbox(textbox2, "Restoring device.  Please wait...");


    progress = create_progress();
    progress->x = 200;
    progress->y = 400;
    progress->w = 800;
    progress->h = 100;
    progress->border = 10;


    data->scenes[8].id = DOWNLOADING;
    data->scenes[8].elements[0].data = textbox;
    data->scenes[8].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[8].elements[1].data = textbox2;
    data->scenes[8].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[8].elements[2].data = progress;
    data->scenes[8].elements[2].draw = MAKEDRAW(redraw_progress);
    data->scenes[8].num_elements = 3;
    data->scenes[8].function = MAKEFUNC(do_download);



    NOTE("Creating scene 10");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Failed...");


    textbox2 = create_textbox();
    textbox2->x = 140;
    textbox2->y = 180;
    textbox2->w = 1000;
    textbox2->h = 128;
    textbox2->data = data;
    set_label_textbox(textbox2, "Unrecoverable error");

    data->scenes[9].id = UNRECOVERABLE;
    data->scenes[9].elements[0].data = textbox;
    data->scenes[9].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[9].elements[1].data = textbox2;
    data->scenes[9].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[9].num_elements = 2;



    NOTE("Creating scene 11");
    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    textbox->data = data;
    set_label_textbox(textbox, "Status: ");
    set_text_textbox(textbox, "Finished");


    textbox2 = create_textbox();
    textbox2->x = 140;
    textbox2->y = 180;
    textbox2->w = 1000;
    textbox2->h = 128;
    textbox2->data = data;
    set_label_textbox(textbox2, " ");
    set_text_textbox(textbox2, "Press any key to reboot");

    data->scenes[10].id = DONE;
    data->scenes[10].elements[0].data = textbox;
    data->scenes[10].elements[0].draw = MAKEDRAW(redraw_textbox);
    data->scenes[10].elements[0].press= MAKEPRESS(do_reboot);
    data->scenes[10].elements[1].data = textbox2;
    data->scenes[10].elements[1].draw = MAKEDRAW(redraw_textbox);
    data->scenes[10].num_elements = 2;


    return 0;
}

static void
prepare_devs(void) {
    int fd;
    fd = open("/dev/null", O_RDONLY);
    if (-1 == fd) {
        if (mkdir("/dev", 0777) == -1)
            PERROR("Unable to mkdir /dev");

        if (mknod("/dev/null", S_IFCHR | 0777, makedev(1, 3)) == -1)
            PERROR("Unable to mknod /dev/null");

        if (mknod("/dev/mem", S_IFCHR | 0777, makedev(1, 1)) == -1)
            PERROR("Unable to mknod /dev/mem");

        if (mknod("/dev/kmem", S_IFCHR | 0777, makedev(1, 2)) == -1)
            PERROR("Unable to mknod /dev/kmem");

        if (mknod("/dev/zero", S_IFCHR | 0777, makedev(1, 5)) == -1)
            PERROR("Unable to mknod /dev/zero");

        if (mknod("/dev/tty", S_IFCHR | 0777, makedev(5, 0)) == -1)
            PERROR("Unable to mknod /dev/tty");

        if (mknod("/dev/ttyGS0", S_IFCHR | 0777, makedev(249, 0)) == -1)
            PERROR("Unable to mknod /dev/ttyGS0");
        errfd = open("/dev/ttyGS0", O_WRONLY);
        if (-1 != errfd) {
            errfil = fdopen(errfd, "w");
        }

        if (mknod("/dev/ttyS0", S_IFCHR | 0777, makedev(4, 64)) == -1)
        fd = open("/dev/ttyS0", O_WRONLY);
        if (-1 != fd)
            dup2(fd, 1);

        if (mkdir("/dev/input", 0777) == -1)
            PERROR("Unable to mkdir /dev/input");
        unlink("/dev/input/event0");
        unlink("/dev/input/event1");
        if (mknod("/dev/input/event0", S_IFCHR | 0777, makedev(13, 64)) == -1)
            PERROR("Unable to mknod /dev/input/event0");
        if (mknod("/dev/input/event1", S_IFCHR | 0777, makedev(13, 65)) == -1)
            PERROR("Unable to mknod /dev/input/event1");
        if (mknod("/dev/fb0", S_IFCHR | 0777, makedev(29, 0)) == -1)
            PERROR("Unable to mknod /dev/fb0");
        if (mknod("/dev/mmcblk0",   S_IFCHR | 0777, makedev(179, 0)) == -1)
            PERROR("Unable to mknod /dev/mmcblk0");
        if (mknod("/dev/mmcblk0p1", S_IFCHR | 0777, makedev(179, 1)) == -1)
            PERROR("Unable to mknod /dev/mmcblk0p1");
        if (mknod("/dev/mmcblk0p2", S_IFCHR | 0777, makedev(179, 2)) == -1)
            PERROR("Unable to mknod /dev/mmcblk0p2");
        if (mknod("/dev/mmcblk0p3", S_IFCHR | 0777, makedev(179, 3)) == -1)
            PERROR("Unable to mknod /dev/mmcblk0p3");
        if (mknod("/dev/mmcblk0p4", S_IFCHR | 0777, makedev(179, 4)) == -1)
            PERROR("Unable to mknod /dev/mmcblk0p4");
        
        NOTE("Finished trying to set up /dev/");
    }
    else
        NOTE("/dev/ was already set up");
    alarm(1);
}

int main(int argc, char **argv) {
    struct recovery_data data;
    SDL_Event e;

    bzero(&data, sizeof(data));

    NOTE("NeTV Recovery version %d.%d.%d", 
         0xff&(RECOVERY_VERSION>>16),
         0xff&(RECOVERY_VERSION>>8 ),
         0xff&(RECOVERY_VERSION>>0 ));

    signal(SIGTERM, sig_handle);
    signal(SIGHUP, sig_handle);
    signal(SIGALRM, sig_handle);

    signal(SIGSEGV, sig_handle);
    signal(SIGTRAP, sig_handle);
    signal(SIGILL, sig_handle);
    signal(SIGFPE, sig_handle);
#ifdef linux
    prepare_devs();
#endif

    bzero(&e, sizeof(e));
    bzero(&data, sizeof(data));

    data.should_quit = 0;

    NOTE("Initializing SDL...");
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO)) {
        fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    NOTE("Initializing TTF engine...");
    if (TTF_Init()) {
        fprintf(stderr, "Couldn't initialize SDL_TTF: %s\n", TTF_GetError());
        return 1;
    }

    SDL_ShowCursor(SDL_DISABLE);

    NOTE("Running udev...");
    udev_main();

    NOTE("Loading modules...");
    my_init_module("/modules/compat_firmware_class.ko");
    my_init_module("/modules/compat.ko");
    my_init_module("/modules/rfkill_backport.ko");
    my_init_module("/modules/cfg80211.ko");
    my_init_module("/modules/ath.ko");
    my_init_module("/modules/ath9k_hw.ko");
    my_init_module("/modules/ath9k_common.ko");
    my_init_module("/modules/mac80211.ko");
    my_init_module("/modules/ath9k_htc.ko");

    NOTE("Setting up scenes...");
    setup_scenes(&data);

    NOTE("Setting video mode...");
    data.screen = SDL_SetVideoMode(1280, 720, 16, 0);
    alarm(0);
    if (!data.screen) {
        fprintf(stderr, "Error: %s\n", SDL_GetError());
        return 0;
    }

    NOTE("Moving to scene %d", SELECT_SSID);
    move_to_scene(&data, SELECT_SSID);
    redraw_scene(&data);

    NOTE("Entering main loop");
    while (!data.should_quit) {

        SDL_WaitEvent(&e);
        switch(e.type) {
            case SDL_QUIT:
                data.should_quit = 1;
                break;

            case SDL_KEYDOWN: {
                SDL_KeyboardEvent *key = (SDL_KeyboardEvent *)&e;

                switch (key->keysym.sym) {
                    case SDLK_UP:
                    case SDLK_DOWN:
                    case SDLK_LEFT:
                    case SDLK_RIGHT:
                    case SDLK_RETURN:

                        process_key(&data, key->keysym.sym);
                        redraw_scene(&data);

                        break;

                    case SDLK_ESCAPE:
                        data.should_quit = 1;
                        SDL_Quit();
                        break;

                    default:
                        break;
                }


                break;
            }

            default:
                break;
        }
    }

    SDL_Quit();
    return 0;
}
