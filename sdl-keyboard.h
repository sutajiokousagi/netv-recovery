#ifndef __SDL_KEYBOARD_H__
#define __SDL_KEYBOARD_H__

#include <SDL/SDL.h>
#include "keyboard.h"

void redraw_keyboard(struct keyboard *kbd, SDL_Surface *screen);
void press_keyboard(struct keyboard *kbd, int key);

#endif /* __SDL_KEYBOARD_H__ */
