/* rgwin_buf.c: The buffer window type
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
#include "rgwin_buf.h"

/* Maximum buffer size. The slack value is how much larger than the size 
    we should get before we trim. */
#define BUFFER_SIZE (5000)
#define BUFFER_SLACK (1000)

static long find_style_by_pos(window_textbuffer_t *dwin, long pos);
static void set_last_run(window_textbuffer_t *dwin, glui32 style);

window_textbuffer_t *win_textbuffer_create(window_t *win)
{
    window_textbuffer_t *dwin = (window_textbuffer_t *)malloc(sizeof(window_textbuffer_t));
    dwin->owner = win;
    
    dwin->numchars = 0;
    dwin->charssize = 500;
    dwin->chars = (glui32 *)malloc(dwin->charssize * sizeof(glui32));
    
    dwin->numruns = 0;
    dwin->runssize = 40;
    dwin->runs = (tbrun_t *)malloc(dwin->runssize * sizeof(tbrun_t));
    
    if (!dwin->chars || !dwin->runs)
        return NULL;

    dwin->inbuf = NULL;
    dwin->inunicode = FALSE;
    dwin->inecho = FALSE;
    dwin->intermkeys = 0;
    
    dwin->numruns = 1;
    dwin->runs[0].style = style_Normal;
    dwin->runs[0].pos = 0;
    
    dwin->dirtybeg = -1;
    dwin->dirtyend = -1;
    dwin->dirtydelta = -1;
    dwin->prev_dirtybeg = -1;
    
    dwin->width = -1;
    dwin->height = -1;

    return dwin;
}

void win_textbuffer_destroy(window_textbuffer_t *dwin)
{
    if (dwin->inbuf) {
        if (gli_unregister_arr) {
            char *typedesc = (dwin->inunicode ? "&+#!Iu" : "&+#!Cn");
            (*gli_unregister_arr)(dwin->inbuf, dwin->inmax, typedesc, dwin->inarrayrock);
        }
        dwin->inbuf = NULL;
    }
    
    dwin->owner = NULL;
    
    if (dwin->runs) {
        free(dwin->runs);
        dwin->runs = NULL;
    }
    
    if (dwin->chars) {
        free(dwin->chars);
        dwin->chars = NULL;
    }
    
    free(dwin);
}

void win_textbuffer_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics)
{
    int oldwid, oldhgt;
    window_textbuffer_t *dwin = win->data;
    dwin->owner->bbox = *box;

    oldwid = dwin->width;
    oldhgt = dwin->height;

    dwin->width = box->right - box->left;
    dwin->height = box->bottom - box->top;
    
    if (oldwid != dwin->width) {
        /* Set dirty region to the whole (or visible?), and
            delta should indicate that the whole old region is changed. */
        if (dwin->dirtybeg == -1) {
            dwin->dirtybeg = 0;
            dwin->dirtyend = dwin->numchars;
            dwin->dirtydelta = 0;
        }
        else {
            dwin->dirtybeg = 0;
            dwin->dirtyend = dwin->numchars;
        }
    }
    else if (oldhgt != dwin->height) {
    }
}

/* Find the last stylerun for which pos >= style.pos. We know run[0].pos == 0,
    so the result is always >= 0. */
static long find_style_by_pos(window_textbuffer_t *dwin, long pos)
{
    long beg, end, val;
    tbrun_t *runs = dwin->runs;
    
    /* Do a binary search, maintaining 
            runs[beg].pos <= pos < runs[end].pos
        (we pretend that runs[numruns].pos is infinity) */
    
    beg = 0;
    end = dwin->numruns;
    
    while (beg+1 < end) {
        val = (beg+end) / 2;
        if (pos >= runs[val].pos) {
            beg = val;
        }
        else {
            end = val;
        }
    }
    
    return beg;
}

data_content_t *win_textbuffer_update(window_t *win)
{
    window_textbuffer_t *dwin = win->data;
    long snum, cnum, spanstart;
    long nextrunpos;
    short curstyle;

    if (dwin->dirtybeg == -1) {
        return NULL;
    }
    dwin->prev_dirtybeg = dwin->dirtybeg;

    data_content_t *dat = data_content_alloc(win->updatetag, win->type);

    /* ### not exactly the right test */
    if (dwin->dirtybeg == 0)
        dat->clear = TRUE;

    if (dwin->dirtybeg < dwin->dirtyend) {
        cnum = dwin->dirtybeg;
        spanstart = cnum;
        snum = find_style_by_pos(dwin, cnum);
        curstyle = dwin->runs[snum].style;
        if (snum+1 < dwin->numruns)
            nextrunpos = dwin->runs[snum+1].pos;
        else
            nextrunpos = dwin->numchars+1;

        data_line_t *line = data_line_alloc();
        gen_list_append(&dat->lines, line);
        line->append = TRUE;

        while (cnum < dwin->dirtyend) {
            glui32 ch = dwin->chars[cnum];
            if (ch == '\n') {
                if (cnum > spanstart) {
                    data_line_add_span(line, curstyle, dwin->chars+spanstart, cnum-spanstart);
                    spanstart = cnum;
                }

                line = data_line_alloc();
                gen_list_append(&dat->lines, line);
                line->append = FALSE;

                cnum++;
                spanstart = cnum;
                continue;
            }

            while (cnum >= nextrunpos) {
                if (cnum > spanstart) {
                    data_line_add_span(line, curstyle, dwin->chars+spanstart, cnum-spanstart);
                    spanstart = cnum;
                }

                snum++;
                curstyle = dwin->runs[snum].style;
                if (snum+1 < dwin->numruns)
                    nextrunpos = dwin->runs[snum+1].pos;
                else
                    nextrunpos = dwin->numchars+1;
            }

            cnum++;
        }

        if (cnum > spanstart) {
            data_line_add_span(line, curstyle, dwin->chars+spanstart, cnum-spanstart);
            spanstart = cnum;
        }
    }

    dwin->dirtybeg = -1;
    dwin->dirtyend = -1;

    return dat;
}

void win_textbuffer_putchar(window_t *win, glui32 ch)
{
    window_textbuffer_t *dwin = win->data;
    long lx;
    
    if (dwin->numchars >= dwin->charssize) {
        dwin->charssize *= 2;
        dwin->chars = (glui32 *)realloc(dwin->chars, 
            dwin->charssize * sizeof(glui32));
    }
    
    lx = dwin->numchars;
    
    if (win->style != dwin->runs[dwin->numruns-1].style) {
        set_last_run(dwin, win->style);
    }
    
    dwin->chars[lx] = ch;
    dwin->numchars++;
    
    if (dwin->dirtybeg == -1) {
        dwin->dirtybeg = lx;
        dwin->dirtyend = lx+1;
        dwin->dirtydelta = 1;
    }
    else {
        if (lx < dwin->dirtybeg)
            dwin->dirtybeg = lx;
        if (lx+1 > dwin->dirtyend)
            dwin->dirtyend = lx+1;
        dwin->dirtydelta += 1;
    }
}

static void set_last_run(window_textbuffer_t *dwin, glui32 style)
{
    long lx = dwin->numchars;
    long rx = dwin->numruns-1;
    
    if (dwin->runs[rx].pos == lx) {
        dwin->runs[rx].style = style;
    }
    else {
        rx++;
        if (rx >= dwin->runssize) {
            dwin->runssize *= 2;
            dwin->runs = (tbrun_t *)realloc(dwin->runs,
                dwin->runssize * sizeof(tbrun_t));
        }
        dwin->runs[rx].pos = lx;
        dwin->runs[rx].style = style;
        dwin->numruns++;
    }

}

void win_textbuffer_clear(window_t *win)
{
    window_textbuffer_t *dwin = win->data;
    long oldlen = dwin->numchars;
    
    dwin->numchars = 0;
    dwin->numruns = 1;
    dwin->runs[0].style = win->style;
    dwin->runs[0].pos = 0;
    
    if (dwin->dirtybeg == -1) {
        dwin->dirtybeg = 0;
        dwin->dirtyend = 0;
        dwin->dirtydelta = -oldlen;
    }
    else {
        dwin->dirtybeg = 0;
        dwin->dirtyend = 0;
        dwin->dirtydelta -= oldlen;
    }
}

void win_textbuffer_trim_buffer(window_t *win)
{
    window_textbuffer_t *dwin = win->data;
    long snum, cnum;
    long rx;
    
    if (dwin->numchars <= BUFFER_SIZE + BUFFER_SLACK)
        return; 
        
    /* We need to knock BUFFER_SLACK chars off the beginning of the buffer, if
        such are conveniently available. */
        
    cnum = dwin->numchars - BUFFER_SIZE;
    if (dwin->dirtybeg != -1 && cnum > dwin->dirtybeg)
        cnum = dwin->dirtybeg;
    
    /* Back up to the previous newline. */
    while (cnum > 0 && dwin->chars[cnum-1] != '\n')
        cnum--;
    if (cnum <= 0)
        return;
    snum = find_style_by_pos(dwin, cnum);
    
    /* trim chars */
    
    if (dwin->numchars > cnum)
        memmove(dwin->chars, &(dwin->chars[cnum]), 
            (dwin->numchars - cnum) * sizeof(glui32));
    dwin->numchars -= cnum;

    if (dwin->dirtybeg == -1) {
        /* nothing dirty; leave it that way. */
    }
    else {
        /* dirty region is after the chunk; quietly slide it back. We already
            know that (dwin->dirtybeg >= cnum). */
        dwin->dirtybeg -= cnum;
        dwin->dirtyend -= cnum;
    }
    
    /* trim runs */
    
    if (snum >= dwin->numruns) {
        short sstyle = dwin->runs[snum].style;
        dwin->runs[0].style = sstyle;
        dwin->runs[0].pos = 0;
        dwin->numruns = 1;
    }
    else {
        for (rx=snum; rx<dwin->numruns; rx++) {
            tbrun_t *srun2 = &(dwin->runs[rx]);
            if (srun2->pos >= cnum)
                srun2->pos -= cnum;
            else
                srun2->pos = 0;
        }
        memmove(dwin->runs, &(dwin->runs[snum]), 
            (dwin->numruns - snum) * sizeof(tbrun_t));
        dwin->numruns -= snum;
    }
    
}

/* Prepare the window for line input. */
void win_textbuffer_init_line(window_t *win, void *buf, int unicode, 
    int maxlen, int initlen)
{
    window_textbuffer_t *dwin = win->data;
    
    dwin->inbuf = buf;
    dwin->inunicode = unicode;
    dwin->inmax = maxlen;
    dwin->incurpos = initlen;
    dwin->inecho = win->echo_line_input;
    dwin->intermkeys = win->terminate_line_input;
    dwin->origstyle = win->style;
    win->style = style_Input;
    set_last_run(dwin, win->style);
    
    if (gli_register_arr) {
        char *typedesc = (dwin->inunicode ? "&+#!Iu" : "&+#!Cn");
        dwin->inarrayrock = (*gli_register_arr)(dwin->inbuf, maxlen, typedesc);
    }
}

void win_textbuffer_prepare_input(window_t *win, glui32 *buf, glui32 len)
{
    window_textbuffer_t *dwin = win->data;
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

void win_textbuffer_accept_line(window_t *win)
{
    long len;
    void *inbuf;
    int inmax, inunicode, inecho;
    glui32 termkey = 0;
    gidispatch_rock_t inarrayrock;
    window_textbuffer_t *dwin = win->data;
    
    if (!dwin->inbuf)
        return;
    
    inbuf = dwin->inbuf;
    inmax = dwin->inmax;
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
                win_textbuffer_putchar(win, ch);
            }
        }
        else {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((glui32 *)inbuf)[ix];
                win_textbuffer_putchar(win, ch);
            }
        }
    }
    
    win->style = dwin->origstyle;
    set_last_run(dwin, win->style);

    /* ### set termkey */

    gli_event_store(evtype_LineInput, win, len, termkey);
    win->line_request = FALSE;
    dwin->inbuf = NULL;
    dwin->incurpos = 0;
    dwin->inmax = 0;
    dwin->inecho = FALSE;
    dwin->intermkeys = 0;

    if (inecho)
        win_textbuffer_putchar(win, '\n');

    if (gli_unregister_arr) {
        char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inmax, typedesc, inarrayrock);
    }
}

/* Abort line input, storing whatever's been typed so far. */
void win_textbuffer_cancel_line(window_t *win, event_t *ev)
{
    long len;
    void *inbuf;
    int inmax, inunicode, inecho;
    gidispatch_rock_t inarrayrock;
    window_textbuffer_t *dwin = win->data;

    if (!dwin->inbuf)
        return;

    inbuf = dwin->inbuf;
    inmax = dwin->inmax;
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
                win_textbuffer_putchar(win, ch);
            }
        }
        else {
            for (ix=0; ix<len; ix++) {
                glui32 ch = ((glui32 *)inbuf)[ix];
                win_textbuffer_putchar(win, ch);
            }
        }
    }
    
    win->style = dwin->origstyle;
    set_last_run(dwin, win->style);

    ev->type = evtype_LineInput;
    ev->win = win;
    ev->val1 = len;
    
    win->line_request = FALSE;
    dwin->inbuf = NULL;
    dwin->incurpos = 0;
    dwin->inmax = 0;
    dwin->inecho = FALSE;
    dwin->intermkeys = 0;

    if (inecho)
        win_textbuffer_putchar(win, '\n');
    
    if (gli_unregister_arr) {
        char *typedesc = (inunicode ? "&+#!Iu" : "&+#!Cn");
        (*gli_unregister_arr)(inbuf, inmax, typedesc, inarrayrock);
    }
}

