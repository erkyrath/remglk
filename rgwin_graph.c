/* rgwin_graph.c: The graphics window type
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "rgwin_graph.h"

window_graphics_t *win_graphics_create(window_t *win)
{
    window_graphics_t *dwin = (window_graphics_t *)malloc(sizeof(window_graphics_t));
    dwin->owner = win;
    
    dwin->numcontent = 0;
    dwin->contentsize = 4;
    dwin->content = (data_specialspan_t **)malloc(dwin->contentsize * sizeof(data_specialspan_t *));

    dwin->updatemark = 0;

    dwin->graphwidth = 0;
    dwin->graphheight = 0;
    
    return dwin;
}

void win_graphics_destroy(window_graphics_t *dwin)
{
    dwin->owner = NULL;
    
    if (dwin->content) {
        long px;
        for (px=0; px<dwin->numcontent; px++)
            data_specialspan_free(dwin->content[px]);
        free(dwin->content);
        dwin->content = NULL;
    }
    
    free(dwin);
}

void win_graphics_clear(window_t *win)
{
    window_graphics_t *dwin = win->data;

    /* If the background color has been set, we must retain that entry.
       (The last setcolor, if there are several.) */

    data_specialspan_t *setcolspan = NULL;
    int setcolmarked = FALSE;

    /* Discard all the content entries, except for the last setcolor. */
    long px;
    for (px=0; px<dwin->numcontent; px++) {
        data_specialspan_t *span = dwin->content[px];
        dwin->content[px] = NULL;

        if (span->type == specialtype_SetColor) {
            if (setcolspan)
                data_specialspan_free(setcolspan);
            setcolspan = span;
            setcolmarked = (px >= dwin->updatemark);
        }
        else {
            data_specialspan_free(span);
        }
    }

    dwin->numcontent = 0;
    dwin->updatemark = 0;

    /* Put back the setcolor (if found). Note that contentsize is at
       least 4. */
    if (setcolspan) {
        dwin->content[dwin->numcontent] = setcolspan;
        dwin->numcontent++;
        if (!setcolmarked)
            dwin->updatemark++;
    }

    /* Clear to background color. */
    data_specialspan_t *fillspan = data_specialspan_alloc(specialtype_Fill);
    dwin->content[dwin->numcontent] = fillspan;
    dwin->numcontent++;
}

void win_graphics_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics)
{
    window_graphics_t *dwin = win->data;
    dwin->owner->bbox = *box;

    int width = box->right - box->left;
    int height = box->bottom - box->top;

    dwin->graphwidth = width - metrics->graphicsmarginx;
    if (dwin->graphwidth < 0)
        dwin->graphwidth = 0;
    dwin->graphheight = height - metrics->graphicsmarginy;
    if (dwin->graphheight < 0)
        dwin->graphheight = 0;
}

void win_graphics_putspecial(window_t *win, data_specialspan_t *span)
{
    window_graphics_t *dwin = win->data;

    if (dwin->numcontent >= dwin->contentsize) {
        dwin->contentsize *= 2;
        dwin->content = (data_specialspan_t **)realloc(dwin->content,
            dwin->contentsize * sizeof(data_specialspan_t *));
    }
    
    dwin->content[dwin->numcontent] = span;
    dwin->numcontent++;
}

data_content_t *win_graphics_update(window_t *win)
{
    window_graphics_t *dwin = win->data;

    data_content_t *dat = NULL;

    if (dwin->numcontent > dwin->updatemark) {
        long px;
        dat = data_content_alloc(win->updatetag, win->type);
        data_line_t *line = data_line_alloc();
        gen_list_append(&dat->lines, line);

        for (px=dwin->updatemark; px<dwin->numcontent; px++) {
            data_specialspan_t *span = dwin->content[px];
            data_line_add_specialspan(line, span);
        }

        dwin->updatemark = dwin->numcontent;
    }

    return dat;
}

void win_graphics_trim_buffer(window_t *win)
{
    window_graphics_t *dwin = win->data;

    /* If a whole-window fill command has been sent, we're going to drop
       all commands before it. Except that we save the last setcolor
       of the trimmed range. */

    long px;
    long lastfill = -1;
    for (px=0; px<dwin->updatemark; px++) {
        data_specialspan_t *span = dwin->content[px];
        if (span->type == specialtype_Fill && !span->hasdimensions) {
            lastfill = px;
        }
    }

    if (lastfill <= 0) {
        return;
    }

    data_specialspan_t *lastsetcol = NULL;
    for (px=0; px<lastfill; px++) {
        data_specialspan_t *span = dwin->content[px];
        dwin->content[px] = NULL;
        if (span->type == specialtype_SetColor) {
            if (lastsetcol)
                data_specialspan_free(lastsetcol);
            lastsetcol = span;
        }
        else {
            data_specialspan_free(span);
        }
    }

    long delta;
    if (!lastsetcol) {
        delta = lastfill;
        if (lastfill > 0 && lastfill < dwin->numcontent)
            memmove(dwin->content, &dwin->content[lastfill],
                (dwin->numcontent-lastfill) * sizeof(data_specialspan_t *));
    }
    else {
        delta = lastfill-1;
        dwin->content[0] = lastsetcol;
        if (lastfill > 1 && lastfill < dwin->numcontent)
            memmove(&dwin->content[1], &dwin->content[lastfill],
                (dwin->numcontent-lastfill) * sizeof(data_specialspan_t *));
    }

    dwin->numcontent -= delta;
    if (dwin->updatemark >= lastfill)
        dwin->updatemark -= delta;
    else
        dwin->updatemark = 0;
}

