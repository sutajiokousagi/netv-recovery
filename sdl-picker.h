#ifndef __SDL_PICKER_H__
#define __SDL_PICKER_H__
#include <SDL/SDL.h>
#include "picker.h"

void redraw_picker(struct picker *picker, SDL_Surface *screen);
void press_picker(struct picker *picker, int key);

#endif /* __SDL_PICKER_H__ */
