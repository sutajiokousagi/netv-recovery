#ifndef __GUNZIP_H__
#define __GUNZIP_H__
int unpack_gz_stream(int in, int out, int (*upd)(void *,int), void *dat);
#endif /* __GUNZIP_H__ */
