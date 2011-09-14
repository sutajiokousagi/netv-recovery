#ifndef __WGET_H__
#define __WGET_H__
int do_wget(char *url,
            int (*progress)(void *data, int current, int total),
            int (*handle)(void *data, char *bytes, int len),
            void *data);
#endif /* __WGET_H__ */
