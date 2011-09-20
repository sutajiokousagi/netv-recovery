#ifndef __PROGRESS_H__
#define __PROGRESS_H__

#define DEFAULT_ENTRY_H 64
#define DEFAULT_ENTRY_H_PAD 4

struct progress {
    int x, y, w, h;

    int dirty;
    int initialized;

    int percent;
    int border;
    int color;

    void *data;
    void *surface;
};

struct progress * create_progress(void);
int set_progress(struct progress *progress, int percent);
void destroy_progress(struct progress *progress);

#endif /* __PROGRESS_H__ */
