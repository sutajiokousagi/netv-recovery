#include "sdl-textbox.h"
#include <SDL_ttf.h>

int
redraw_textbox(struct textbox *textbox, SDL_Surface *screen)
{
    SDL_Surface *tmp;
    SDL_Rect r;
    int w, h;
    SDL_Color c;

    if (!textbox->font)
        textbox->font = TTF_OpenFont("AMD.ttf", 24);
    if (!textbox->font)
        fprintf(stderr, "Couldn't load font: %s\n", TTF_GetError());

    if (!textbox->label_font)
        textbox->label_font = TTF_OpenFont("AMD.ttf", 24);
    if (!textbox->label_font)
        fprintf(stderr, "Couldn't load font: %s\n", TTF_GetError());

    TTF_SizeText(textbox->label_font, textbox->label, &w, &h);


    c.r = 0;
    c.g = 0;
    c.b = 255;
    tmp = TTF_RenderText_Solid(textbox->label_font, textbox->label, c);

    if (!tmp) {
        fprintf(stderr, "Unable to render text: %s\n", TTF_GetError());
        return 1;
    }
    r.w = tmp->w;
    r.h = tmp->h;
    r.x = textbox->x;
    r.y = textbox->y;
    SDL_BlitSurface(tmp, NULL, screen, &r);

    SDL_FreeSurface(tmp);



    if (textbox->string && strlen(textbox->string)) {
        TTF_SizeText(textbox->font, textbox->string, &w, &h);
        c.r = 255;
        c.g = 255;
        c.b = 255;
        tmp = TTF_RenderText_Solid(textbox->font, textbox->string, c);

        if (!tmp) {
            fprintf(stderr, "Unable to render text: %s\n", TTF_GetError());
            return 1;
        }
        r.x += r.w + TEXTBOX_LABEL_PADDING; /* Reuse r.w from previous render */
        r.y = textbox->y;
        r.w = tmp->w;
        r.h = tmp->h;
        SDL_BlitSurface(tmp, NULL, screen, &r);
        SDL_FreeSurface(tmp);
    }

    return 0;
}
