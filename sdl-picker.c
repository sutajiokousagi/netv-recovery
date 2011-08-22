#include <SDL_ttf.h>
#include "sdl-picker.h"

static void
draw_string_to_surface(SDL_Surface *surface,
                       int x, int y,
                       char *str, TTF_Font *font, SDL_Color c)
{
    SDL_Surface *tmp;
    SDL_Rect r;
    int w, h;

    TTF_SizeText(font, str, &w, &h);
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
redraw_picker(struct picker *picker, SDL_Surface *screen) {
    SDL_Rect r, r2;
    int h_offset;
    SDL_Surface *entry_surface;
    unsigned int rmask, gmask, bmask, amask;
    int entry_i;

    SDL_Color normal_color, highlight_color;

    normal_color.r = 0;
    normal_color.g = 255;
    normal_color.b = 0;

    highlight_color.r = 53;
    highlight_color.g = 0;
    highlight_color.g = 193;


#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    rmask = 0xff000000;
    gmask = 0x00ff0000;
    bmask = 0x0000ff00;
    amask = 0x000000ff;
#else
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = 0xff000000;
#endif

    if (!picker->font)
        picker->font = TTF_OpenFont("DejaVuSans.ttf", 24);
    if (!picker->font)
        fprintf(stderr, "Couldn't load font: %s\n", TTF_GetError());

    r.x = picker->x-1;
    r.y = picker->y-1;
    r.w = picker->w+2;
    r.h = picker->h+2;

    SDL_FillRect(screen, &r, 0xff00ff00);
    r.x++;
    r.y++;
    r.w-=2;
    r.h-=2;
    SDL_FillRect(screen, &r, 0xff000000);

    entry_surface = SDL_CreateRGBSurface(0, picker->w-2, picker->entry_h, 32,
            rmask, gmask, bmask, amask);

    h_offset = picker->y + 2;
    for (entry_i=picker->first_entry;
         entry_i<picker->entry_count && h_offset < picker->y + picker->h;
         entry_i++) {
        SDL_FillRect(entry_surface, NULL, 0xff452369);

        if (entry_i == picker->active_entry)
            draw_string_to_surface(entry_surface, 0, picker->entry_h/2,
                picker->entries[entry_i], picker->font, highlight_color);
        else
            draw_string_to_surface(entry_surface, 0, picker->entry_h/2,
                picker->entries[entry_i], picker->font, normal_color);

        r2.x = 0;
        r2.y = 0;
        r2.w = entry_surface->w;
        r2.h = entry_surface->h;

        r.x = picker->x;
        r.y = h_offset;
        r.w = picker->w;
        r.h = picker->entry_h;

        if (r.h + r.y > picker->y + picker->h) {
            r2.h = (picker->y + picker->h) - r.y;
        }

        SDL_BlitSurface(entry_surface, &r2, screen, &r);
        h_offset += picker->entry_h + picker->entry_h_pad;
    }
    SDL_FreeSurface(entry_surface);
}

void
press_picker(struct picker *picker, int key)
{
    if (key == SDLK_DOWN && picker->active_entry+1 < picker->entry_count)
            picker->active_entry++;
    if (key == SDLK_UP && picker->active_entry > 0)
            picker->active_entry--;
    if (key == SDLK_RETURN) {
        if (!picker->pick_item) {
            fprintf(stderr, "No pick_item function defined\n");
            return;
        }
        picker->pick_item(picker->entries[picker->active_entry], picker->data);
        return;
    }

    /* Scroll up if necessary */
    if (picker->active_entry < picker->first_entry)
        picker->first_entry = picker->active_entry;

    /* Scroll down if necessary */
    if ( (picker->entry_h + picker->entry_h_pad) *
            (picker->active_entry+1-picker->first_entry) >
            picker->h)
        picker->first_entry++;

}

