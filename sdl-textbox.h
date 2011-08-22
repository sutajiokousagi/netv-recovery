#ifndef __SDL_TEXTBOX_H__
#define __SDL_TEXTBOX_H__
#include "textbox.h"
#include <SDL/SDL.h>

#define TEXTBOX_LABEL_PADDING 5

int redraw_textbox(struct textbox *textbox, SDL_Surface *screen);
#endif /* __SDL_TEXTBOX_H__ */
