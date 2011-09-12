#include <SDL/SDL.h>
#include <SDL_ttf.h>
#include "sdl-keyboard.h"
#include "sdl-picker.h"
#include "sdl-textbox.h"

#define ICON_W 64
#define ICON_H 64
#define ICON_PADDING 8

#define SELECT_SSID 1
#define TYPE_SSID 2
#define SELECT_KEY 3
#define TYPE_KEY 4
#define START_CONNECTING 5
#define CONNECTING 6

#define ENC_OPEN 0
#define ENC_WPA 1

#define OTHER_NETWORK_STRING "[Other Network]"

struct recovery_data {
    struct keyboard *kbd;
    struct picker *ssids; /* Selectable SSIDs */
    struct picker *select_encryption; /* Selectable encryption */
    struct textbox *title_textbox;

    char *ssid;
    char *key;

    int active;
    int encryption_type;
    int should_quit;
};

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

    if (data->active == TYPE_SSID
     || data->active == TYPE_KEY) {
        char *str = get_text_textbox(data->title_textbox);
        int new_str_len = 1;
        if (str)
            new_str_len = strlen(str)+1;

        if (key == '\b') {
            if (new_str_len > 1)
                str[new_str_len-2] = '\0';
            set_text_textbox(data->title_textbox, str);
        }
        else if(key == '\n') {
            if (data->active == TYPE_SSID) {
                set_ssid(data, str);
                data->active = SELECT_KEY;
            }
            else if(data->active == TYPE_KEY) {
                set_key(data, str);
                data->active = START_CONNECTING;
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
            set_text_textbox(data->title_textbox, newstr);
        }
    }

    return 0;
}


static int
pick_encryption(char *item, void *_data)
{
    struct recovery_data *data = _data;

    if (data->select_encryption->active_entry == 0) {
        fprintf(stderr, "Using open encryption\n");
        data->encryption_type = ENC_OPEN;
        data->active = START_CONNECTING;
    }
    else {
        fprintf(stderr, "Using WPA\n");
        data->encryption_type = ENC_WPA;
        data->active = TYPE_KEY;
        set_label_textbox(data->title_textbox, "Key: ");
        set_text_textbox(data->title_textbox, "");
    }
    return 0;
}


static int
pick_ssid(char *item, void *_data)
{
    struct recovery_data *data = _data;

    data->title_textbox = create_textbox();
    data->title_textbox->x = 140;
    data->title_textbox->y = 80;
    data->title_textbox->w = 1000;
    data->title_textbox->h = 128;
    set_label_textbox(data->title_textbox, "Network: ");

    if (data->ssids->active_entry == data->ssids->entry_count-1) {
        set_text_textbox(data->title_textbox, " ");
        data->active = TYPE_SSID;
    }
    else {
        set_text_textbox(data->title_textbox, item);
        set_ssid(data, item);
        data->active = SELECT_KEY;
    }
    return 0;
}


int main(int argc, char **argv) {
    struct recovery_data data;
    SDL_Surface *screen;
    SDL_Event e;


    bzero(&e, sizeof(e));
    bzero(&data, sizeof(data));

    data.active = SELECT_SSID;
    data.should_quit = 0;

    if (SDL_Init(SDL_INIT_EVERYTHING)) {
        fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init()) {
        fprintf(stderr, "Couldn't initialize SDL_TTF: %s\n", TTF_GetError());
        return 1;
    }

    data.kbd = create_keyboard(KEYS, SHIFTS);
    data.kbd->x = 120;
    data.kbd->y = 270;
    data.kbd->send_key = pressed_key;
    data.kbd->data = &data;

    data.ssids = create_picker();
    data.ssids->data = &data;
    data.ssids->pick_item = pick_ssid;

    data.select_encryption = create_picker();
    data.select_encryption->data = &data;
    data.select_encryption->pick_item = pick_encryption;

    add_item_to_picker(data.select_encryption, "Open");
    add_item_to_picker(data.select_encryption, "WPA");


    /* Start out by drawing the keyboard afresh */
    //redraw_keyboard(kbd, screen);

    data.ssids->x = 140;
    data.ssids->y = 120;
    data.ssids->w = 1000;
    data.ssids->h = 500;


    data.select_encryption->x = 140;
    data.select_encryption->y = 120;
    data.select_encryption->w = 1000;
    data.select_encryption->h = 500;

    add_item_to_picker(data.ssids, "Test SSID");
    add_item_to_picker(data.ssids, "Test 2 SSID");
    add_item_to_picker(data.ssids, "Test 3 SSID");
    /*
    add_item_to_picker(data.ssids, "Lorem");
    add_item_to_picker(data.ssids, "ipsum");
    add_item_to_picker(data.ssids, "dolor");
    add_item_to_picker(data.ssids, "sit");
    add_item_to_picker(data.ssids, "dolum");
    */
    add_item_to_picker(data.ssids, OTHER_NETWORK_STRING);


    screen = SDL_SetVideoMode(1280, 720, 16, 0);
    if (!screen) {
        fprintf(stderr, "Error: %s\n", SDL_GetError());
        return 0;
    }
    redraw_picker(data.ssids, screen);
    SDL_Flip(screen);

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

                        SDL_FillRect(screen, NULL, 0);
                        /* Process the input */
                        if(SELECT_SSID == data.active)
                            press_picker(data.ssids, key->keysym.sym);
                        else if (TYPE_SSID == data.active)
                            press_keyboard(data.kbd, key->keysym.sym);
                        else if(SELECT_KEY == data.active)
                            press_picker(data.select_encryption, key->keysym.sym);
                        else if (TYPE_KEY == data.active)
                            press_keyboard(data.kbd, key->keysym.sym);

                        /* Redraw the [possibly-new] widget */
                        if(SELECT_SSID == data.active)
                            redraw_picker(data.ssids, screen);
                        else if (TYPE_SSID == data.active)
                            redraw_keyboard(data.kbd, screen);
                        else if(SELECT_KEY == data.active)
                            redraw_picker(data.select_encryption, screen);
                        else if(TYPE_KEY == data.active)
                            redraw_keyboard(data.kbd, screen);

                        if (data.title_textbox)
                            redraw_textbox(data.title_textbox, screen);

                        SDL_Flip(screen);
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
