#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include "textbox.h"

struct textbox *
create_textbox(void)
{
    struct textbox *new = malloc(sizeof(struct textbox));
    bzero(new, sizeof(*new));
    return new;
}

int
set_text_textbox(struct textbox *textbox, char *string)
{
    if (textbox->string)
        free(textbox->string);
    textbox->string = malloc(strlen(string)+1);
    strcpy(textbox->string, string);
    return 0;
}

int
set_label_textbox(struct textbox *textbox, char *string)
{
    if (textbox->label)
        free(textbox->label);
    textbox->label = malloc(strlen(string)+1);
    strcpy(textbox->label, string);
    return 0;
}

char *get_text_textbox(struct textbox *textbox)
{
    return textbox->string;
}

char *get_label_textbox(struct textbox *textbox)
{
    return textbox->label;
}
