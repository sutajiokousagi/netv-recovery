#include <SDL/SDL.h>
#include <SDL_ttf.h>
#include "keyboard.h"

#define ICON_W 64
#define ICON_H 64
#define ICON_PADDING 8


static void
draw_string_to_surface(SDL_Surface *surface,
                       int x, int y,
                       char *str, TTF_Font *font, SDL_Color c)
{
    SDL_Surface *tmp;
    SDL_Rect r;
    int w, h;

    TTF_SizeText(font, str, &w, &h);
    x -= w/2;
    y -= h/2;

    tmp = TTF_RenderText_Solid(font, str, c);
    if (!tmp) {
        fprintf(stderr, "Unable to render text: %s\n", TTF_GetError());
        return;
    }
    r.w = tmp->w;
    r.h = tmp->h;
    r.x = x;
    r.y = y;
    SDL_BlitSurface(tmp, NULL, surface, &r);
    SDL_FreeSurface(tmp);
}
    

void
redraw_keyboard(struct keyboard *kbd,
                SDL_Surface *screen)
{
    int c, r;
    int row_offset;
    
    SDL_Color normal_color, highlight_color;

    normal_color.r = 0;
    normal_color.g = 0;
    normal_color.b = 0;

    highlight_color.r = 255;
    highlight_color.g = 255;
    highlight_color.b = 255;

    if (!kbd->font)
        kbd->font = TTF_OpenFont("AMD.ttf", 24);
    if (!kbd->font)
        fprintf(stderr, "Couldn't load font: %s\n", TTF_GetError());

    row_offset = kbd->y;
    for (r=0; r<kbd->row_count; r++) {
        int col_offset = kbd->x;

        for (c=0; c<kbd->rows[r].col_count; c++) {
            struct button *b = &kbd->buttons[kbd->rows[r].key_start+c];

            if (!b->w) {
                if (b->keycode == '\t')
                    b->w = ICON_W + 32;
                else if(b->keycode == '\a')
                    b->w = ICON_W + 48;
                else if(b->keycode == '\r')
                    b->w = ICON_W + 88;
                else if(b->keycode == '\b')
                    b->w = ICON_W + 32;
                else if(b->keycode == '\n')
                    b->w = ICON_W + 56;
                else if(b->keycode == ' ')
                    b->w = ICON_W*12;
                else
                    b->w = ICON_W;
            }

            if (!b->h)
                b->h = ICON_H;

            if (!b->x) {
                if (b->keycode == ' ')
                    b->x = col_offset + 128;
                else
                    b->x = col_offset;
            }

            if (!b->y)
                b->y = row_offset;

            col_offset = b->x + b->w + ICON_PADDING;


            if (!b->image)
                b->image = (void *)SDL_CreateRGBSurface(0, b->w, b->h, 32,
                        0xff, 0xff00, 0xff0000, 0xff000000);


            if (b == kbd->current_button
             || (b->keycode == '\a' && kbd->shifted == -1)
             || (b->keycode == '\r' && kbd->shifted == 1)) {
                SDL_FillRect(b->image, NULL, 0xffff0000);
                draw_string_to_surface(b->image, b->w/2, b->h/2,
                        kbd->shifted?b->str_shift:b->str, kbd->font, highlight_color);
            }
            else {
                SDL_FillRect(b->image, NULL, 0xff0000ff);
                draw_string_to_surface(b->image, b->w/2, b->h/2,
                        kbd->shifted?b->str_shift:b->str, kbd->font, normal_color);
            }
        }

        row_offset += ICON_PADDING+ICON_H;
    }

    for (c=0; c<kbd->button_count; c++) {
        SDL_Rect r;
        r.x = kbd->buttons[c].x;
        r.y = kbd->buttons[c].y;
        r.w = kbd->buttons[c].w;
        r.h = kbd->buttons[c].h;
        SDL_BlitSurface(kbd->buttons[c].image, NULL, screen, &r);
    }
}


void
press_keyboard(struct keyboard *kbd, int key)
{
    if (key == SDLK_UP
        && kbd->current_button->up)
        kbd->current_button = kbd->current_button->up;

    if (key == SDLK_LEFT
        && kbd->current_button->left)
        kbd->current_button = kbd->current_button->left;

    if (key == SDLK_RIGHT
        && kbd->current_button->right)
        kbd->current_button = kbd->current_button->right;

    if (key == SDLK_DOWN
        && kbd->current_button->down)
        kbd->current_button = kbd->current_button->down;

    if (key == SDLK_RETURN) {

        if (kbd->current_button->keycode == '\a') {
            if (kbd->shifted == -1)
                kbd->shifted = 0;
            else
                kbd->shifted = -1;
            fprintf(stderr, "Toggling capslock: %d\n",
                    kbd->shifted);
        }

        else if (kbd->current_button->keycode == '\r') {
            kbd->shifted = !kbd->shifted;
            fprintf(stderr, "Toggling shift: %d\n",
                    kbd->shifted);
        }

        else {
            if (kbd->send_key)
                kbd->send_key(kbd->shifted
                                ? kbd->current_button->shift_keycode
                                : kbd->current_button->keycode,
                              kbd->data);
            else
                fprintf(stderr, "No send_key() defined\n");

            /* Reset the shift key (do not reset caps lock key) */
            if (kbd->shifted >= 0)
                kbd->shifted = 0;
        }
    }
}


