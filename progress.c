#include <stdlib.h>
#include <string.h>
#include "progress.h"

struct progress *
create_progress(void)
{
    struct progress *new = malloc(sizeof(struct progress));
    bzero(new, sizeof(*new));
    new->border = 10;
    new->dirty = 1;
    return new;
}

int
set_progress(struct progress *progress, int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent < progress->percent)
        progress->dirty = 1;
    if (percent > 100)
        percent = 100;
    progress->percent = percent;
    return 0;
}

void
destroy_progress(struct progress *progress)
{
    free(progress);
}
