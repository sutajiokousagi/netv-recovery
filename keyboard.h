#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

/* List of keys.  \v indicates a new row.  \0 indicates end-of-definition */
#define KEYS "`1234567890-=\b\v\tqwertyuiop[]\\\v\aasdfghjkl;'\n\v\rzxcvbnm,./\r\v "
#define SHIFTS "~!@#$%^&*()_+\b\v\tQWERTYUIOP{}|\v\aASDFGHJKL:\"\n\v\rZXCVBNM<>?\r\v "


struct button {
    int x, y, w, h;
    void *image;

    struct button *left, *right, *up, *down;
    int row, col;
    char keycode, shift_keycode;
    char str[10], str_shift[10];

    void *data;
};

struct row {
    int col_count;
    int key_start;
};

struct keyboard {
    struct button *buttons;
    struct button *current_button;
    struct row *rows;
    int row_count, col_count, button_count;
    int x, y;
    int shifted;

    void *data;
    void *font;

    int (*send_key)(int key, void *data);
};

struct keyboard *create_keyboard(const char *keys, const char *shifts);

#endif /* __KEYBOARD_H__ */
