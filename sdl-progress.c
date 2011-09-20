#include <SDL/SDL.h>
#include "progress.h"

static int init_bar(struct progress *progress, SDL_Surface *screen) {
    progress->surface = SDL_CreateRGBSurface(0, progress->w, progress->h,
            screen->format->BitsPerPixel,
            screen->format->Rmask,
            screen->format->Gmask,
            screen->format->Bmask,
            screen->format->Amask);
    progress->color = SDL_MapRGB(screen->format, 50, 40, 192);
    progress->initialized = 1;
    return 0;
}

static int reinit_bar(struct progress *progress, SDL_Surface *screen) {
    SDL_Rect r;
    SDL_FillRect(progress->surface, NULL, SDL_MapRGB(screen->format, 50, 60, 70));
    r.x = progress->border-5;
    r.y = progress->border-5;
    r.w = progress->w - (progress->border*2)+10;
    r.h = progress->h - (progress->border*2)+10;
    SDL_FillRect(progress->surface, &r, SDL_MapRGB(screen->format, 0, 0, 0));
    progress->dirty = 0;
    return 0;
}

void redraw_progress(struct progress *progress, SDL_Surface *screen) {
    SDL_Rect r;

    if (!progress->initialized)
        init_bar(progress, screen);
    if (progress->dirty)
        reinit_bar(progress, screen);

    r.x = progress->border;
    r.y = progress->border;
    r.w = progress->w - (progress->border*2);
    r.h = progress->h - (progress->border*2);

    r.w = (r.w * progress->percent / 100);

    SDL_FillRect(progress->surface, &r, progress->color);

    r.x = progress->x;
    r.y = progress->y;
    r.w = progress->w;
    r.h = progress->h;
    SDL_BlitSurface(progress->surface, NULL, screen, &r);
    return;
}
