#include <stdlib.h>
#include <string.h>
#include "picker.h"

struct picker *
create_picker(void)
{
    struct picker *new = malloc(sizeof(struct picker));
    bzero(new, sizeof(*new));
    new->entry_h = DEFAULT_ENTRY_H;
    new->entry_h_pad = DEFAULT_ENTRY_H_PAD;
    return new;
}

int
add_item_to_picker(struct picker *picker, char *item)
{
    int i;

    if (!item)
        return 0;

    /* Look for the item in the picker, don't allow duplicates */
    for (i=0; i<picker->entry_count; i++)
        if (!strcmp(picker->entries[i], item))
            return 1;

    picker->entry_count++;
    picker->entries=realloc(picker->entries, picker->entry_count*sizeof(char*));
    picker->entries[picker->entry_count-1] = malloc(strlen(item)+1);
    strcpy(picker->entries[picker->entry_count-1], item);
    return 0;
}

int
clear_picker(struct picker *picker)
{
    int i;
    for (i=0; i<picker->entry_count; i++)
        free(picker->entries[i]);
    free(picker->entries);
    picker->entries = NULL;
    picker->entry_count = 0;
    return 0;
}

void
destroy_picker(struct picker *picker)
{
    int i;

    for (i=0; i<picker->entry_count; i++)
        free(picker->entries[i]);
    free(picker->entries);
    free(picker);
}
