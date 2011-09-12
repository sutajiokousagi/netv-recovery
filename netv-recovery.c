
#include <SDL/SDL.h>
#include <SDL_ttf.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#ifdef linux
#include <linux/kd.h>
#endif

#include "sdl-keyboard.h"
#include "sdl-picker.h"
#include "sdl-textbox.h"
#include "wpa-controller.h"

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

#define ENC_OPEN 0
#define ENC_WPA 1

#define OTHER_NETWORK_STRING "[Other Network]"
struct recovery_data;

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
    struct scene scenes[10];

    char *ssid;
    char *key;

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

    picker = (struct picker *)data->scene->elements[0].data;

    if (picker->active_entry == picker->entry_count-1) {
        move_to_scene(data, TYPE_SSID);
    }
    else {
        set_ssid(data, item);
        move_to_scene(data, SELECT_ENCRYPTION);
    }
    return 0;
}

static int
establish_connection(struct recovery_data *data)
{
    struct wpa_process *process;
    int ret;

    process = start_wpa(data->ssid, data->encryption_type==ENC_WPA ? data->key : NULL);
    if (!process) {
        move_to_scene(data, SELECT_ENCRYPTION);
        return -1;
    }

    do {
        ret = poll_wpa(process, 1);
    } while (!ret);
    fprintf(stderr, "Connected!\n");


    if (ret < 0) {
        move_to_scene(data, CONNECTION_ERROR);
        stop_wpa(process);
    }
    else
        move_to_scene(data, CONNECTED);

    redraw_scene(data);

    return 0;
}

static void sig_cleanup(int sig) {
    fprintf(stderr, "Got sig %d\n", sig);
    SDL_Quit();
    exit(1);
}

#ifdef linux
static void
fix_tty(char *tty_name)
{
    int tty;
    tty = open(tty_name, O_RDWR);
    if (tty >= 0) {
        ioctl(tty, KDSETMODE, KD_TEXT);
        close(tty);
    }
}
#endif


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
            data->scene = data->scenes+i;
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

    /* Select SSID */
    picker = create_picker();
    picker->x = 140;
    picker->y = 120;
    picker->w = 1000;
    picker->h = 500;
    picker->data = data;
    picker->pick_item = pick_ssid;
#ifdef __APPLE__
    add_item_to_picker(picker, "Test SSID");
    add_item_to_picker(picker, "Test 2 SSID");
    add_item_to_picker(picker, "Test 3 SSID");
#endif
    /*
    add_item_to_picker(picker, "Lorem");
    add_item_to_picker(picker, "ipsum");
    add_item_to_picker(picker, "dolor");
    add_item_to_picker(picker, "sit");
    add_item_to_picker(picker, "dolum");
    */
    add_item_to_picker(picker, OTHER_NETWORK_STRING);


    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    set_label_textbox(textbox, "Select network");
    set_text_textbox(textbox, " ");

    data->scenes[0].id = SELECT_SSID;
    data->scenes[0].elements[0].data = picker;
    data->scenes[0].elements[0].draw = redraw_picker;
    data->scenes[0].elements[0].press = press_picker;
    data->scenes[0].elements[1].data = textbox;
    data->scenes[0].elements[1].draw = redraw_textbox;
    data->scenes[0].num_elements = 2;



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
    set_label_textbox(textbox, "Network name: ");
    set_text_textbox(textbox, "");

    data->scenes[1].id = TYPE_SSID;
    data->scenes[1].elements[0].data = kbd;
    data->scenes[1].elements[0].draw = redraw_keyboard;
    data->scenes[1].elements[0].press = press_keyboard;
    data->scenes[1].elements[1].data = textbox;
    data->scenes[1].elements[1].draw = redraw_textbox;
    data->scenes[1].num_elements = 2;



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
    set_label_textbox(textbox, "Select encryption type");

    add_item_to_picker(picker, "Open");
    add_item_to_picker(picker, "WPA");

    data->scenes[2].id = SELECT_ENCRYPTION;
    data->scenes[2].elements[0].data = picker;
    data->scenes[2].elements[0].draw = redraw_picker;
    data->scenes[2].elements[0].press = press_picker;
    data->scenes[2].elements[1].data = textbox;
    data->scenes[2].elements[1].draw = redraw_textbox;
    data->scenes[2].num_elements = 2;



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
    set_label_textbox(textbox, "Password: ");

    data->scenes[3].id = TYPE_KEY;
    data->scenes[3].elements[0].data = kbd;
    data->scenes[3].elements[0].draw = redraw_keyboard;
    data->scenes[3].elements[0].press = press_keyboard;
    data->scenes[3].elements[1].data = textbox;
    data->scenes[3].elements[1].draw = redraw_textbox;
    data->scenes[3].num_elements = 2;




    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    set_label_textbox(textbox, "Status ");
    set_text_textbox(textbox, "Connecting...");

    data->scenes[4].id = START_CONNECTING;
    data->scenes[4].elements[0].data = textbox;
    data->scenes[4].elements[0].draw = redraw_textbox;
    data->scenes[4].num_elements = 1;
    data->scenes[4].function = establish_connection;



    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    set_label_textbox(textbox, "Status ");
    set_text_textbox(textbox, "Connected");

    data->scenes[5].id = CONNECTED;
    data->scenes[5].elements[0].data = textbox;
    data->scenes[5].elements[0].draw = redraw_textbox;
    data->scenes[5].num_elements = 1;



    textbox = create_textbox();
    textbox->x = 140;
    textbox->y = 80;
    textbox->w = 1000;
    textbox->h = 128;
    set_label_textbox(textbox, "Status ");
    set_text_textbox(textbox, "Error");

    data->scenes[6].id = CONNECTION_ERROR;
    data->scenes[6].elements[0].data = textbox;
    data->scenes[6].elements[0].draw = redraw_textbox;
    data->scenes[6].num_elements = 1;

    return 0;
}

int main(int argc, char **argv) {
    struct recovery_data data;
    SDL_Event e;

    bzero(&data, sizeof(data));


    signal(SIGTERM, sig_cleanup);
#ifdef linux
    unlink("/dev/tty");
    mknod("/dev/tty", S_IFCHR | 0777, makedev(4, 2));
    fix_tty("/dev/tty");
    fix_tty("/dev/tty0");
    fix_tty("/dev/tty1");
    fix_tty("/dev/tty2");
    fix_tty("/dev/tty3");
    fix_tty("/dev/tty4");
#endif

    bzero(&e, sizeof(e));
    bzero(&data, sizeof(data));

    data.should_quit = 0;

    setenv("SDL_NOMOUSE", "1", 1);
    if (SDL_Init(SDL_INIT_EVERYTHING)) {
        fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init()) {
        fprintf(stderr, "Couldn't initialize SDL_TTF: %s\n", TTF_GetError());
        return 1;
    }

    setup_scenes(&data);


    data.screen = SDL_SetVideoMode(1280, 720, 16, 0);
    if (!data.screen) {
        fprintf(stderr, "Error: %s\n", SDL_GetError());
        return 0;
    }

    move_to_scene(&data, SELECT_SSID);
    redraw_scene(&data);

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

                        if (data.scene->function)
                            data.scene->function(&data);

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
