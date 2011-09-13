#ifndef __PICKER_H__
#define __PICKER_H__

#define DEFAULT_ENTRY_H 64
#define DEFAULT_ENTRY_H_PAD 4

struct picker {
    int x, y, w, h;
    int entry_h;
    int entry_h_pad;

    char **entries;
    int entry_count;
    int active_entry;
    int first_entry;

    void *data;
    void *font;
    int (*pick_item)(char *entry, void *data);
};

struct picker * create_picker(void);
int add_item_to_picker(struct picker *picker, char *item);
int clear_picker(struct picker *picker);
void destroy_picker(struct picker *picker);

#endif /* __PICKER_H__ */
