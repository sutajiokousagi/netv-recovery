#ifndef __TEXTBOX_H__
#define __TEXTBOX_H__

struct textbox {
    void *data;
    int x, y, w, h;

    char *string;
    char *label;

    /* To be overwritten with platform-specific implementations */
    void *font;
    void *label_font;
    void *image;
};

struct textbox *create_textbox(void);
int set_text_textbox(struct textbox *textbox, char *string);
int set_label_textbox(struct textbox *textbox, char *string);

char *get_text_textbox(struct textbox *textbox);
char *get_label_textbox(struct textbox *textbox);

#endif /* __TEXTBOX_H__ */
