#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "keyboard.h"



static char *printable_key(char c, char *str, int str_size) {
    if (c == '\a')
        snprintf(str, str_size, "capslock");
    else if (c == '\v')
        snprintf(str, str_size, "--error--");
    else if (c == '\r')
        snprintf(str, str_size, "shift");
    else if (c == '\t')
        snprintf(str, str_size, "tab");
    else if (c == '\n')
        snprintf(str, str_size, "return");
    else if (c == '\b')
        snprintf(str, str_size, "delete");
    else if (c == ' ')
        snprintf(str, str_size, "space");
    else
        snprintf(str, str_size, "%c", c);
    return str;
}


static struct button *calculate_left(struct keyboard *kbd, int r, int c) {
    if (c <= 0)
        return NULL;
    return &kbd->buttons[kbd->rows[r].key_start+c-1];
}

static struct button *calculate_right(struct keyboard *kbd, int r, int c) {
    if (c+2 > kbd->rows[r].col_count)
        return NULL;
    return &kbd->buttons[kbd->rows[r].key_start+c+1];
}

static struct button *calculate_up(struct keyboard *kbd, int r, int c) {
    if (r <= 0)
        return NULL;
    return &kbd->buttons[kbd->rows[r-1].key_start+c];
}

static struct button *calculate_down(struct keyboard *kbd, int r, int c) {
    if (r+2 > kbd->row_count)
        return NULL;
    if (c+1 > kbd->rows[r+1].col_count)
        return &kbd->buttons[kbd->rows[r+1].key_start+kbd->rows[r+1].col_count-1];
    return &kbd->buttons[kbd->rows[r+1].key_start+c];
}

static void wire_keyboard(struct keyboard *kbd) {
    int r, c;

    /* Wire up the directions */
    for (r=0; r<kbd->row_count; r++) {
        for (c=0; c<kbd->rows[r].col_count; c++) {

            kbd->buttons[kbd->rows[r].key_start+c].left =
                calculate_left(kbd, r, c);
            kbd->buttons[kbd->rows[r].key_start+c].right =
                calculate_right(kbd, r, c);
            kbd->buttons[kbd->rows[r].key_start+c].up =
                calculate_up(kbd, r, c);
            kbd->buttons[kbd->rows[r].key_start+c].down =
                calculate_down(kbd, r, c);
        }
    }
}


struct keyboard *create_keyboard(const char *keys, const char *shifts) {
    struct keyboard *kbd = NULL;

    kbd = malloc(sizeof(struct keyboard));
    bzero(kbd, sizeof(struct keyboard));

    for (kbd->row_count=0;
         keys[kbd->button_count] && shifts[kbd->button_count];
         kbd->row_count++) {

        kbd->rows = realloc(kbd->rows, sizeof(struct row)*(kbd->row_count+1));
        bzero(&kbd->rows[kbd->row_count], sizeof(struct row));
        kbd->rows[kbd->row_count].key_start = kbd->button_count;

        for (kbd->rows[kbd->row_count].col_count=0;
             keys[kbd->button_count] && shifts[kbd->button_count];
             kbd->button_count++, kbd->rows[kbd->row_count].col_count++) {

            kbd->buttons = realloc(kbd->buttons,
                                sizeof(struct button)*(kbd->button_count+1));
            bzero(&kbd->buttons[kbd->button_count], sizeof(struct button));

            if (keys[kbd->button_count] == '\v') {
                kbd->button_count++;
                break;
            }

            kbd->buttons[kbd->button_count].row = kbd->row_count;
            kbd->buttons[kbd->button_count].col = kbd->rows[kbd->row_count].col_count;
            kbd->buttons[kbd->button_count].keycode =
                keys[kbd->button_count];
            kbd->buttons[kbd->button_count].shift_keycode =
                shifts[kbd->button_count];
            printable_key(keys[kbd->button_count],
                          kbd->buttons[kbd->button_count].str,
                          sizeof(kbd->buttons[kbd->button_count].str));
            printable_key(shifts[kbd->button_count],
                          kbd->buttons[kbd->button_count].str_shift,
                          sizeof(kbd->buttons[kbd->button_count].str_shift));
        }
    }

    kbd->current_button = kbd->buttons;

    wire_keyboard(kbd);

    return kbd;
}

/*
static void test_keyboard(struct keyboard *kbd) {
    char buffer[128];
    struct button *b;
    b = &kbd->buttons[0];

    fprintf(stderr, "Currently at %s/%s\n", b->str, b->str_shift);
    while(read(0, buffer, sizeof(buffer)) > 0) {
        if (buffer[0] == 'w' && b->up)
            b = b->up;
        if (buffer[0] == 'a' && b->left)
            b = b->left;
        if (buffer[0] == 's' && b->down)
            b = b->down;
        if (buffer[0] == 'd' && b->right)
            b = b->right;

        fprintf(stderr, "Currently at %s/%s\n", b->str, b->str_shift);
    }
}


int main(int argc, char **argv) {
    struct keyboard *kbd;
    kbd = create_keyboard();
    test_keyboard(kbd);

    return 0;
}
*/
