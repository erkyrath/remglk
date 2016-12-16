/* rgwin_grid.c: The grid window type
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "rgwin_grid.h"

/* A grid of characters. We store the window as a list of lines (see
    gtwgrid.h); within a line, just store an array of characters and
    an array of style bytes, the same size. (If we ever have more than
    255 styles, things will have to be changed, but that's unlikely.)
*/

static void init_lines(window_textgrid_t *dwin, int beg, int end, int linewid);


window_textgrid_t *win_textgrid_create(window_t *win)
{
    window_textgrid_t *dwin = (window_textgrid_t *)malloc(sizeof(window_textgrid_t));
    dwin->owner = win;
    
    dwin->width = 0;
    dwin->height = 0;
    
    dwin->curx = 0;
    dwin->cury = 0;
    
    dwin->linessize = 0;
    dwin->lines = NULL;
    
    dwin->inbuf = NULL;
    dwin->inunicode = FALSE;
    dwin->inecho = FALSE;
    
    return dwin;
}

void win_textgrid_destroy(window_textgrid_t *dwin)
{
    if (dwin->inbuf) {
        if (gli_unregister_arr) {
            char *typedesc = (dwin->inunicode ? "&+#!Iu" : "&+#!Cn");
            (*gli_unregister_arr)(dwin->inbuf, dwin->inoriglen, typedesc, dwin->inarrayrock);
        }
        dwin->inbuf = NULL;
    }
    
    dwin->owner = NULL;
    if (dwin->lines) {
        int jx;
        for (jx=0; jx<dwin->linessize; jx++) {
            tgline_t *ln = &(dwin->lines[jx]);
            if (ln->chars) {
                free(ln->chars);
                ln->chars = NULL;
            }
            if (ln->styles) {
                free(ln->styles);
                ln->styles = NULL;
            }
            if (ln->links) {
                free(ln->links);
                ln->links = NULL;
            }
        }
        
        free(dwin->lines);
        dwin->lines = NULL;
    }
    free(dwin);
}

void win_textgrid_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics)
{
    int ix, jx, oldval;
    int newwid, newhgt;
    window_textgrid_t *dwin = win->data;
    dwin->owner->bbox = *box;
    
    newwid = (((box->right - box->left) - metrics->gridmarginx) / metrics->gridcharwidth);
    newhgt = (((box->bottom - box->top) - metrics->gridmarginy) / metrics->gridcharheight);
    
    if (dwin->lines == NULL) {
        dwin->linessize = (newhgt+1);
        dwin->lines = (tgline_t *)malloc(dwin->linessize * sizeof(tgline_t));
        if (!dwin->lines)
            return;
        init_lines(dwin, 0, dwin->linessize, newwid);
    }
    else {
        if (newhgt > dwin->linessize) {
            oldval = dwin->linessize;
            dwin->linessize = (newhgt+1) * 2;
            dwin->lines = (tgline_t *)realloc(dwin->lines, 
                dwin->linessize * sizeof(tgline_t));
            if (!dwin->lines)
                return;
            init_lines(dwin, oldval, dwin->linessize, newwid);
        }
        if (newhgt > dwin->height) {
            /* Clear any new lines */
            for (jx=dwin->height; jx<newhgt; jx++) {
                tgline_t *ln = &(dwin->lines[jx]);
                for (ix=0; ix<ln->allocsize; ix++) {
                    ln->chars[ix] = ' ';
                    ln->styles[ix] = style_Normal;
                    ln->links[ix] = 0;
                }
            }
        }
        for (jx=0; jx<newhgt; jx++) {
            tgline_t *ln = &(dwin->lines[jx]);
            if (newwid > ln->allocsize) {
                oldval = ln->allocsize;
                ln->allocsize = (newwid+1) * 2;
                ln->chars = (glui32 *)realloc(ln->chars, 
                    ln->allocsize * sizeof(glui32));
                ln->styles = (short *)realloc(ln->styles, 
                    ln->allocsize * sizeof(short));
                ln->links = (glui32 *)realloc(ln->links, 
                    ln->allocsize * sizeof(glui32));
                if (!ln->chars || !ln->styles) {
                    dwin->lines = NULL;
                    return;
                }
                for (ix=oldval; ix<ln->allocsize; ix++) {
                    ln->chars[ix] = ' ';
                    ln->styles[ix] = style_Normal;
                    ln->links[ix] = 0;
                }
            }
        }
    }
    
    dwin->width = newwid;
    dwin->height = newhgt;

    dwin->alldirty = TRUE;
}

static void init_lines(window_textgrid_t *dwin, int beg, int end, int linewid)
{
    int ix, jx;

    for (jx=beg; jx<end; jx++) {
        tgline_t *ln = &(dwin->lines[jx]);
        ln->allocsize = (linewid+1);
        ln->dirty = TRUE;
        ln->chars = (glui32 *)malloc(ln->allocsize * sizeof(glui32));
        ln->styles = (short *)malloc(ln->allocsize * sizeof(short));
        ln->links = (glui32 *)malloc(ln->allocsize * sizeof(glui32));
        if (!ln->chars || !ln->styles || !ln->links) {
            dwin->lines = NULL;
            return;
        }
        for (ix=0; ix<ln->allocsize; ix++) {
            ln->chars[ix] = ' ';
            ln->styles[ix] = style_Normal;
            ln->links[ix] = 0;
        }
    }
}

data_content_t *win_textgrid_update(window_t *win)
{
    int jx, ix;
    int spanstart;
    short curstyle;
    glui32 curlink;
    window_textgrid_t *dwin = win->data;

    if (!dwin->lines) {
        dwin->alldirty = FALSE;
        return NULL;
    }

    data_content_t *dat = NULL;

    for (jx=0; jx<dwin->height; jx++) {
        tgline_t *ln = &(dwin->lines[jx]);
        if (!dwin->alldirty && !ln->dirty)
            continue;

        if (!dat)
            dat = data_content_alloc(win->updatetag, win->type);

        data_line_t *line = data_line_alloc();
        gen_list_append(&dat->lines, line);
        line->linenum = jx;

        curstyle = -1;
        curlink = 0;
        spanstart = 0;
        for (ix=0; ix<dwin->width; ix++) {
            if (ln->styles[ix] != curstyle || ln->links[ix] != curlink) {
                if (ix > spanstart) {
                    data_line_add_span(line, curstyle, curlink, ln->chars+spanstart, ix-spanstart);
                    spanstart = ix;
                }

                curstyle = ln->styles[ix];
                curlink = ln->links[ix];
            }
        }
        if (ix > spanstart) {
            data_line_add_span(line, curstyle, curlink, ln->chars+spanstart, ix-spanstart);
            spanstart = ix;
        }

        ln->dirty = FALSE;
    }

    dwin->alldirty = FALSE;

    return dat;
}

void win_textgrid_putchar(window_t *win, glui32 ch)
{
    window_textgrid_t *dwin = win->data;
    tgline_t *ln;
    
    /* Canonicalize the cursor position. That is, the cursor may have been
        left outside the window area; wrap it if necessary. */
    if (dwin->curx < 0)
        dwin->curx = 0;
    else if (dwin->curx >= dwin->width) {
        dwin->curx = 0;
        dwin->cury++;
    }
    if (dwin->cury < 0)
        dwin->cury = 0;
    else if (dwin->cury >= dwin->height)
        return; /* outside the window */
    
    if (ch == '\n') {
        /* a newline just moves the cursor. */
        dwin->cury++;
        dwin->curx = 0;
        return;
    }
    
    ln = &(dwin->lines[dwin->cury]);
    ln->dirty = TRUE;
    
    ln->chars[dwin->curx] = ch;
    ln->styles[dwin->curx] = win->style;
    ln->links[dwin->curx] = win->hyperlink;
    
    dwin->curx++;
    /* We can leave the cursor outside the window, since it will be
        canonicalized next time a character is printed. */
}

void win_textgrid_clear(window_t *win)
{
    int ix, jx;
    window_textgrid_t *dwin = win->data;
    
    for (jx=0; jx<dwin->height; jx++) {
        tgline_t *ln = &(dwin->lines[jx]);
        for (ix=0; ix<dwin->width; ix++) {
            ln->chars[ix] = ' ';
            ln->styles[ix] = style_Normal;
            ln->links[ix] = 0;
        }
        ln->dirty = TRUE;
    }

    dwin->alldirty = TRUE;
    
    dwin->curx = 0;
    dwin->cury = 0;
}

void win_textgrid_move_cursor(window_t *win, int xpos, int ypos)
{
    window_textgrid_t *dwin = win->data;
    
    /* If the values are negative, they're really huge positive numbers -- 
        remember that they were cast from glui32. So set them huge and
        let canonicalization take its course. */
    if (xpos < 0)
        xpos = 32767;
    if (ypos < 0)
        ypos = 32767;
        
    dwin->curx = xpos;
    dwin->cury = ypos;
}

/* Prepare the window for line input. */
void win_textgrid_init_line(window_t *win, void *buf, int unicode,
    int maxlen, int initlen)
{
    window_textgrid_t *dwin = win->data;
    
    /* Canonicalize the cursor position a little. */
    if (dwin->curx >= dwin->width) {
        dwin->curx = 0;
        dwin->cury++;
    }
    if (dwin->cury >= dwin->height) {
        /* Outside the window; put the cursor in the bottom right. */
        dwin->curx = dwin->width-1;
        dwin->cury = dwin->height-1;
    }

    dwin->inbuf = buf;
    dwin->inunicode = unicode;
    dwin->inoriglen = maxlen;
    if (maxlen > (dwin->width - dwin->curx))
        maxlen = (dwin->width - dwin->curx);
    dwin->inmax = maxlen;
    dwin->inecho = win->echo_line_input;
    dwin->intermkeys = win->terminate_line_input;
    dwin->origstyle = win->style;
    win->style = style_Input;
    
    if (initlen > maxlen)
        initlen = maxlen;
        
    if (gli_register_arr) {
        char *typedesc = (dwin->inunicode ? "&+#!Iu" : "&+#!Cn");
        dwin->inarrayrock = (*gli_register_arr)(dwin->inbuf, dwin->inoriglen, typedesc);
    }
}

void win_textgrid_prepare_input(window_t *win, glui32 *buf, glui32 len)
{
    window_textgrid_t *dwin = win->data;
    int ix;

    if (!dwin->inbuf)
        return;

    if (len > dwin->inmax)
        len = dwin->inmax;

    dwin->incurpos = len;

    if (!dwin->inunicode) {
        char *inbuf = ((char *)dwin->inbuf);
        for (ix=0; ix<len; ix++) {
            glui32 ch = buf[ix];
            if (!(ch >= 0 && ch < 256))
                ch = '?';
            inbuf[ix] = ch;
        }
    }
    else {
        glui32 *inbuf = ((glui32 *)dwin->inbuf);
        for (ix=0; ix<len; ix++) {
            inbuf[ix] = buf[ix];
        }
    }
}

void win_textgrid_accept_line(window_t *win)
{
    long len;
    void *inbuf;
    int inmax, inoriglen, inunicode, inecho;
    glui32 termkey = 0;
    gidispatch_rock_t inarrayrock;
    window_textgrid_t *dwin = win->data;
    
    if (!dwin->inbuf)
        return;
    
    inbuf = dwin->inbuf;
    inmax = dwin->inmax;
    inoriglen = dwin->inoriglen;
    inarrayrock = dwin->inarrayrock;
    inunicode = dwin->inunicode;
    inecho = dwin->inecho;

    len = dwin->incurpos;
    if (inecho && win->echostr) {
        if (!inunicode)
            gli_stream_echo_line(win->echostr, (char *)inbuf, len);
        else
            gli_stream_echo_line_uni(win->echostr, (glui32 *)inbuf, len);
    }
    
    if (inecho) {
        /* Add the typed text to the grid. */
        int ix;
        if (!inunicode) {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((char *)inbuf)[ix];
                win_textgrid_putchar(win, ch);
            }
        }
        else {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((glui32 *)inbuf)[ix];
                win_textgrid_putchar(win, ch);
            }
        }

        dwin->cury = dwin->cury+1;
        dwin->curx = 0;
    }
    
    win->style = dwin->origstyle;

    /* ### set termkey */

    gli_event_store(evtype_LineInput, win, len, termkey);
    win->line_request = FALSE;
    dwin->inbuf = NULL;
    dwin->inoriglen = 0;
    dwin->incurpos = 0;
    dwin->inmax = 0;
    dwin->inecho = FALSE;
    dwin->intermkeys = 0;

    if (gli_unregister_arr) {
        char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inoriglen, typedesc, inarrayrock);
    }
}

/* Abort line input, storing whatever's been typed so far. */
void win_textgrid_cancel_line(window_t *win, event_t *ev)
{
    void *inbuf;
    int inoriglen, inmax, inunicode, inecho, len;
    gidispatch_rock_t inarrayrock;
    window_textgrid_t *dwin = win->data;

    if (!dwin->inbuf)
        return;
    
    inbuf = dwin->inbuf;
    inmax = dwin->inmax;
    inoriglen = dwin->inoriglen;
    inarrayrock = dwin->inarrayrock;
    inunicode = dwin->inunicode;
    inecho = dwin->inecho;

    len = dwin->incurpos;
    if (inecho && win->echostr) {
        if (!inunicode)
            gli_stream_echo_line(win->echostr, (char *)inbuf, len);
        else
            gli_stream_echo_line_uni(win->echostr, (glui32 *)inbuf, len);
    }

    if (inecho) {
        /* Add the typed text to the buffer. */
        int ix;
        if (!inunicode) {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((char *)inbuf)[ix];
                win_textgrid_putchar(win, ch);
            }
        }
        else {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((glui32 *)inbuf)[ix];
                win_textgrid_putchar(win, ch);
            }
        }

        dwin->cury = dwin->cury+1;
        dwin->curx = 0;
    }

    win->style = dwin->origstyle;

    ev->type = evtype_LineInput;
    ev->win = win;
    ev->val1 = len;
    
    win->line_request = FALSE;
    dwin->inbuf = NULL;
    dwin->inoriglen = 0;
    dwin->inmax = 0;
    dwin->intermkeys = 0;

    if (gli_unregister_arr) {
        char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inoriglen, typedesc, inarrayrock);
    }
}
