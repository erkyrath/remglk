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
#include "rgwin_buf.h"

/* Maximum buffer size. The slack value is how much larger than the size 
    we should get before we trim. */
#define BUFFER_SIZE (5000)
#define BUFFER_SLACK (1000)

static void final_lines(window_textbuffer_t *dwin, long beg, long end);
static long find_style_by_pos(window_textbuffer_t *dwin, long pos);
static long find_line_by_pos(window_textbuffer_t *dwin, long pos);
static void set_last_run(window_textbuffer_t *dwin, glui32 style);
static void import_input_line(window_textbuffer_t *dwin, void *buf, 
    int unicode, long len);
static void export_input_line(void *buf, int unicode, long len, char *chars);

window_textbuffer_t *win_textbuffer_create(window_t *win)
{
    int ix;
    window_textbuffer_t *dwin = (window_textbuffer_t *)malloc(sizeof(window_textbuffer_t));
    dwin->owner = win;
    
    dwin->numchars = 0;
    dwin->charssize = 500;
    dwin->chars = (char *)malloc(dwin->charssize * sizeof(char));
    
    dwin->numlines = 0;
    dwin->linessize = 50;
    dwin->lines = (tbline_t *)malloc(dwin->linessize * sizeof(tbline_t));
    
    dwin->numruns = 0;
    dwin->runssize = 40;
    dwin->runs = (tbrun_t *)malloc(dwin->runssize * sizeof(tbrun_t));
    
    dwin->tmplinessize = 40;
    dwin->tmplines = (tbline_t *)malloc(dwin->tmplinessize * sizeof(tbline_t));
    
    dwin->tmpwordssize = 40;
    dwin->tmpwords = (tbword_t *)malloc(dwin->tmpwordssize * sizeof(tbword_t));
    
    if (!dwin->chars || !dwin->runs || !dwin->lines 
        || !dwin->tmplines || !dwin->tmpwords)
        return NULL;

    dwin->inbuf = NULL;
    dwin->inunicode = FALSE;
    dwin->inecho = FALSE;
    dwin->intermkeys = 0;
    
    dwin->numruns = 1;
    dwin->runs[0].style = style_Normal;
    dwin->runs[0].pos = 0;
    
    if (pref_historylen > 1) {
        dwin->history = (char **)malloc(sizeof(char *) * pref_historylen);
        if (!dwin->history)
            return NULL;
        for (ix=0; ix<pref_historylen; ix++)
            dwin->history[ix] = NULL;
    }
    else {
        dwin->history = NULL;
    }
    dwin->historypos = 0;
    dwin->historyfirst = 0;
    dwin->historypresent = 0;
    
    dwin->dirtybeg = -1;
    dwin->dirtyend = -1;
    dwin->dirtydelta = -1;
    dwin->scrollline = 0;
    dwin->scrollpos = 0;
    dwin->lastseenline = 0;
    dwin->drawall = TRUE;
    
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
    
    if (dwin->tmplines) {
        /* don't try to destroy tmplines; they're all invalid */
        free(dwin->tmplines);
        dwin->tmplines = NULL;
    }
    
    if (dwin->lines) {
        final_lines(dwin, 0, dwin->numlines);
        free(dwin->lines);
        dwin->lines = NULL;
    }
    
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

static void final_lines(window_textbuffer_t *dwin, long beg, long end)
{
    long lx;
    
    for (lx=beg; lx<end; lx++) {
        tbline_t *ln = &(dwin->lines[lx]);
        if (ln->words) {
            free(ln->words);
            ln->words = NULL;
        }
    }
}

void win_textbuffer_rearrange(window_t *win, grect_t *box)
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

/* Find the last line for which pos >= line.pos. If pos is before the first 
    line.pos, or if there are no lines, this returns -1. Lines always go to
    the end of the text, so this will never be numlines or higher. */
static long find_line_by_pos(window_textbuffer_t *dwin, long pos)
{
    long lx;
    
    if (dwin->numlines == 0)
        return -1;
    
    for (lx=dwin->numlines-1; lx >= 0; lx--) {
        if (pos >= dwin->lines[lx].pos)
            return lx;
    }
    
    return -1;
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

/* This does layout on a segment of text, writing into tmplines. Returns
    the number of lines laid. Assumes tmplines is entirely unused, 
    initially. */
static long layout_chars(window_textbuffer_t *dwin, long chbeg, long chend,
    int startpara)
{
    long cx, cx2, lx, rx;
    long numwords; 
    long linestartpos;
    char ch;
    int lastlinetype;
    short style;
    long styleendpos;
    /* cache some values */
    char *chars = dwin->chars;
    tbrun_t *runs = dwin->runs;
    
    lastlinetype = (startpara) ? wd_EndLine : wd_Text;
    cx = chbeg;
    linestartpos = chbeg;
    lx = 0;
    numwords = 0; /* actually number of tmpwords */
    
    rx = find_style_by_pos(dwin, chbeg);
    style = runs[rx].style;
    if (rx+1 >= dwin->numruns)
        styleendpos = chend+1;
    else
        styleendpos = runs[rx+1].pos;
    
    /* create lines until we reach the end of the text segment; but if the 
        last line ends with a newline, go one more. */
    
    while (numwords || cx < chend || lastlinetype != wd_EndPage) {
        tbline_t *ln;
        int lineover, lineeatto, lastsolidwd;
        long lineeatpos = 0;
        long wx, wx2;
        int linewidth, widthsofar;
        int linetype = 0;
        
        if (lx+2 >= dwin->tmplinessize) {
            /* leaves room for a final line */
            dwin->tmplinessize *= 2;
            dwin->tmplines = (tbline_t *)realloc(dwin->tmplines, 
                dwin->tmplinessize * sizeof(tbline_t));
        }
        ln = &(dwin->tmplines[lx]);
        lx++;
        
        lineover = FALSE;
        lineeatto = 0;
        lastsolidwd = 0;
        widthsofar = 0;
        linewidth = dwin->width;
        
        wx = 0;
        
        while ((wx < numwords || cx < chend) && !lineover) {
            tbword_t *wd;
            
            if (wx >= numwords) {
                /* suck down a new word. */
                
                if (wx+2 >= dwin->tmpwordssize) {
                    /* leaves room for a split word */
                    dwin->tmpwordssize *= 2;
                    dwin->tmpwords = (tbword_t *)realloc(dwin->tmpwords, 
                        dwin->tmpwordssize * sizeof(tbword_t));
                }
                wd = &(dwin->tmpwords[wx]);
                wx++;
                numwords++;
                
                ch = chars[cx];
                cx2 = cx;
                cx++;
                if (ch == '\n') {
                    wd->type = wd_EndLine;
                    wd->pos = cx2;
                    wd->len = 0;
                    wd->style = style;
                }
                else if (ch == ' ') {
                    wd->type = wd_Blank;
                    wd->pos = cx2;
                    while (cx < chend 
                            && cx < styleendpos && chars[cx] == ' ')
                        cx++;
                    wd->len = cx - (wd->pos);
                    wd->style = style;
                }
                else {
                    wd->type = wd_Text;
                    wd->pos = cx2;
                    while (cx < chend 
                            && cx < styleendpos && chars[cx] != '\n' 
                            && chars[cx] != ' ')
                        cx++;
                    wd->len = cx - (wd->pos);
                    wd->style = style;
                }
                
                if (cx >= styleendpos) {
                    rx++;
                    style = runs[rx].style;
                    if (rx+1 >= dwin->numruns)
                        styleendpos = chend+1;
                    else
                        styleendpos = runs[rx+1].pos;
                }
            }
            else {
                /* pull out an existing word. */
                wd = &(dwin->tmpwords[wx]);
                wx++;
            }
            
            if (wd->type == wd_EndLine) {
                lineover = TRUE;
                lineeatto = wx;
                lineeatpos = wd->pos+1;
                linetype = wd_EndLine;
            }
            else {
                if (wd->type == wd_Blank 
                        || widthsofar + wd->len <= linewidth) {
                    widthsofar += wd->len;
                }
                else {
                    /* last text word goes over. */
                    for (wx2 = wx-1; 
                        wx2 > 0 && dwin->tmpwords[wx2-1].type == wd_Text; 
                        wx2--) { }
                    /* wx2 is now the first text word of this group, which 
                        is to say right after the last blank word. If this
                        is > 0, chop there; otherwise we have to split a
                        word somewhere. */
                    if (wx2 > 0) {
                        lineover = TRUE;
                        lineeatto = wx2;
                        lineeatpos = dwin->tmpwords[wx2].pos;
                        linetype = wd_Text;
                    }
                    else {
                        /* first group goes over; gotta split. But we know
                            the last word of the group is the culprit. */
                        int extra = widthsofar + wd->len - linewidth;
                        /* extra is the amount hanging outside the boundary; 
                            will be > 0. */
                        if (wd->len == extra) {
                            /* the whole last word is hanging out. Just 
                                chop. */
                            lineover = TRUE;
                            lineeatto = wx-1;
                            lineeatpos = wd->pos;
                            linetype = wd_Text;
                        }
                        else {
                            /* split the last word, creating a new one. */
                            tbword_t *wd2;
                            if (wx < numwords) {
                                memmove(dwin->tmpwords+(wx+1), 
                                    dwin->tmpwords+wx, 
                                    (numwords-wx) * sizeof(tbword_t));
                            }
                            wd2 = &(dwin->tmpwords[wx]);
                            wx++;
                            numwords++;
                            wd->len -= extra;
                            wd2->type = wd->type;
                            wd2->style = wd->style;
                            wd2->pos = wd->pos+wd->len;
                            wd2->len = extra;
                            lineover = TRUE;
                            lineeatto = wx-1;
                            lineeatpos = wd2->pos;
                            linetype = wd_Text;
                        }
                    }
                }
            }
        }
        
        if (!lineover) {
            /* ran out of characters, no newline */
            lineeatto = wx;
            lineeatpos = chend;
            linetype = wd_EndPage;
        }
        
        if (lineeatto) {
            ln->words = (tbword_t *)malloc(lineeatto * sizeof(tbword_t));
            memcpy(ln->words, dwin->tmpwords, lineeatto * sizeof(tbword_t));
            ln->numwords = lineeatto;
            
            if (lineeatto < numwords) {
                memmove(dwin->tmpwords, 
                    dwin->tmpwords+lineeatto, 
                    (numwords-lineeatto) * sizeof(tbword_t));
            }
            numwords -= lineeatto;
        }
        else {
            ln->words = NULL;
            ln->numwords = 0;
        }
        ln->pos = linestartpos;
        ln->len = 0;
        ln->printwords = 0;
        for (wx2=0; wx2<ln->numwords; wx2++) {
            tbword_t *wd2 = &(ln->words[wx2]);
            ln->len += wd2->len;
            if (wd2->type != wd_EndLine && ln->len <= linewidth)
                ln->printwords = wx2+1;
        }
        linestartpos = lineeatpos;
        ln->startpara = (lastlinetype != wd_Text);
        lastlinetype = linetype;
        
        /* numwords, linestartpos, and startpara values are carried around
            to beginning of loop. */
    }
    
    return lx;
}

/* Replace lines[oldbeg, oldend) with tmplines[0, newnum). The replaced lines
    are deleted; the tmplines array winds up invalid (so it will not need to
    be deleted.) */
static void replace_lines(window_textbuffer_t *dwin, long oldbeg, long oldend,
    long newnum)
{
    long lx, diff;
    tbline_t *lines; /* cache */
    
    diff = newnum - (oldend - oldbeg);
    /* diff is the amount which lines will grow or shrink. */
    
    if (dwin->numlines+diff > dwin->linessize) {
        while (dwin->numlines+diff > dwin->linessize)
            dwin->linessize *= 2;
        dwin->lines = (tbline_t *)realloc(dwin->lines, 
            dwin->linessize * sizeof(tbline_t));
    }
    
    if (oldend > oldbeg)
        final_lines(dwin, oldbeg, oldend);
    
    lines = dwin->lines;

    if (diff != 0) {
        /* diff may be positive or negative */
        if (oldend < dwin->numlines)
            memmove(&(lines[oldend+diff]), &(lines[oldend]), 
                (dwin->numlines - oldend) * sizeof(tbline_t));
    }
    dwin->numlines += diff;
    
    if (newnum)
        memcpy(&(lines[oldbeg]), dwin->tmplines, newnum * sizeof(tbline_t));
        
    if (dwin->scrollline > oldend) {
        dwin->scrollline += diff;
    }
    else if (dwin->scrollline >= oldbeg) {
        dwin->scrollline = find_line_by_pos(dwin, dwin->scrollpos);
    }
    else {
        /* leave scrollline alone */
    }

    if (dwin->lastseenline > oldend) {
        dwin->lastseenline += diff;
    }
    else if (dwin->lastseenline >= oldbeg) {
        dwin->lastseenline = oldbeg;
    }
    else {
        /* leave lastseenline alone */
    }

    if (dwin->scrollline > dwin->numlines - dwin->height)
        dwin->scrollline = dwin->numlines - dwin->height;
    if (dwin->scrollline < 0)
        dwin->scrollline = 0;
}

static void updatetext(window_textbuffer_t *dwin)
{
    long drawbeg, drawend;
    
    if (dwin->dirtybeg != -1) {
        long numtmplines;
        long chbeg, chend; /* changed region */
        long oldchbeg, oldchend; /* old extent of changed region */
        long lnbeg, lnend; /* lines being replaced */
        long lndelta;
        int startpara;
        
        chbeg = dwin->dirtybeg;
        chend = dwin->dirtyend;
        oldchbeg = dwin->dirtybeg;
        oldchend = dwin->dirtyend - dwin->dirtydelta;
        
        /* push ahead to next newline or end-of-text (still in the same
            line as dirtyend, though). move chend and oldchend in parallel,
            since (outside the changed region) nothing has changed. */
        while (chend < dwin->numchars && dwin->chars[chend] != '\n') {
            chend++;
            oldchend++;
        }
        lnend = find_line_by_pos(dwin, oldchend) + 1;
        
        /* back up to beginning of line, or previous newline, whichever 
            is first */
        lnbeg = find_line_by_pos(dwin, oldchbeg); 
        if (lnbeg >= 0) {
            oldchbeg = dwin->lines[lnbeg].pos;
            chbeg = oldchbeg;
            startpara = dwin->lines[lnbeg].startpara;
        }
        else {
            lnbeg = 0;
            while (chbeg && dwin->chars[chbeg-1] != '\n') {
                chbeg--;
                oldchbeg--;
            }
            startpara = TRUE;
        }
        
        /* lnend is now the first line not to replace. [0..numlines]
            lnbeg is the first line *to* replace [0..numlines) */
        
        numtmplines = layout_chars(dwin, chbeg, chend, startpara);
        dwin->dirtybeg = -1;
        dwin->dirtyend = -1;
        dwin->dirtydelta = -1;
        
        replace_lines(dwin, lnbeg, lnend, numtmplines);
        lndelta = numtmplines - (lnend-lnbeg);
        
        drawbeg = lnbeg;
        if (lndelta == 0) {
            drawend = lnend;
        }
        else {
            if (lndelta > 0)
                drawend = dwin->numlines;
            else
                drawend = dwin->numlines - lndelta;
        }
    }
    else {
        drawbeg = 0;
        drawend = 0;
    }
    
    if (dwin->drawall) {
        drawbeg = dwin->scrollline;
        drawend = dwin->scrollline + dwin->height;
        dwin->drawall = FALSE;
    }
    
    if (drawend > drawbeg) {
        long lx, wx;
        int ix;
        int physln;
        int orgx, orgy;
        
        if (drawbeg < dwin->scrollline)
            drawbeg = dwin->scrollline;
        if (drawend > dwin->scrollline + dwin->height)
            drawend = dwin->scrollline + dwin->height;
        
        orgx = dwin->owner->bbox.left;
        orgy = dwin->owner->bbox.top;
        
        for (lx=drawbeg; lx<drawend; lx++) {
            /*###*/
        }
    }
}

void win_textbuffer_update(window_t *win)
{
    window_textbuffer_t *dwin = win->data;
    updatetext(dwin);
}

void win_textbuffer_putchar(window_t *win, char ch)
{
    window_textbuffer_t *dwin = win->data;
    long lx;
    
    if (dwin->numchars >= dwin->charssize) {
        dwin->charssize *= 2;
        dwin->chars = (char *)realloc(dwin->chars, 
            dwin->charssize * sizeof(char));
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

/* This assumes that the text is all within the final style run. 
    Convenient, but true, since this is only used by editing in the
    input text. */
static void put_text(window_textbuffer_t *dwin, char *buf, long len, 
    long pos, long oldlen)
{
    long diff = len - oldlen;
    
    if (dwin->numchars + diff > dwin->charssize) {
        while (dwin->numchars + diff > dwin->charssize)
            dwin->charssize *= 2;
        dwin->chars = (char *)realloc(dwin->chars, 
            dwin->charssize * sizeof(char));
    }
    
    if (diff != 0 && pos+oldlen < dwin->numchars) {
        memmove(dwin->chars+(pos+len), dwin->chars+(pos+oldlen), 
            (dwin->numchars - (pos+oldlen) * sizeof(char)));
    }
    if (len > 0) {
        memmove(dwin->chars+pos, buf, len * sizeof(char));
    }
    dwin->numchars += diff;
    
    if (dwin->inbuf) {
        if (dwin->incurs >= pos+oldlen)
            dwin->incurs += diff;
        else if (dwin->incurs >= pos)
            dwin->incurs = pos+len;
    }
    
    if (dwin->dirtybeg == -1) {
        dwin->dirtybeg = pos;
        dwin->dirtyend = pos+len;
        dwin->dirtydelta = diff;
    }
    else {
        if (pos < dwin->dirtybeg)
            dwin->dirtybeg = pos;
        if (pos+len > dwin->dirtyend)
            dwin->dirtyend = pos+len;
        dwin->dirtydelta += diff;
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

    dwin->scrollline = 0;
    dwin->scrollpos = 0;
    dwin->lastseenline = 0;
    dwin->drawall = TRUE;
}

void win_textbuffer_trim_buffer(window_t *win)
{
    window_textbuffer_t *dwin = win->data;
    long trimsize;
    long lnum, snum, cnum;
    long lx, wx, rx;
    tbline_t *ln;
    
    if (dwin->numchars <= BUFFER_SIZE + BUFFER_SLACK)
        return; 
        
    /* We need to knock BUFFER_SLACK chars off the beginning of the buffer, if
        such are conveniently available. */
        
    trimsize = dwin->numchars - BUFFER_SIZE;
    if (dwin->dirtybeg != -1 && trimsize > dwin->dirtybeg)
        trimsize = dwin->dirtybeg;
    if (dwin->inbuf && trimsize > dwin->infence) 
        trimsize = dwin->infence;
    
    lnum = find_line_by_pos(dwin, trimsize);
    if (lnum <= 0)
        return;
    /* The trimsize point is at the beginning of lnum, or inside it. So lnum
        will be the first remaining line. */
        
    ln = &(dwin->lines[lnum]);
    cnum = ln->pos;
    if (cnum <= 0)
        return;
    snum = find_style_by_pos(dwin, cnum);
    
    /* trim chars */
    
    if (dwin->numchars > cnum)
        memmove(dwin->chars, &(dwin->chars[cnum]), 
            (dwin->numchars - cnum) * sizeof(char));
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
    
    /* trim lines */
    
    final_lines(dwin, 0, lnum);
    for (lx=lnum; lx<dwin->numlines; lx++) {
        tbline_t *ln2 = &(dwin->lines[lx]);
        ln2->pos -= cnum;
        for (wx=0; wx<ln2->numwords; wx++) {
            tbword_t *wd = &(ln2->words[wx]);
            wd->pos -= cnum;
        }
    }

    if (lnum < dwin->numlines)
        memmove(&(dwin->lines[0]), &(dwin->lines[lnum]), 
            (dwin->numlines - lnum) * sizeof(tbline_t));
    dwin->numlines -= lnum;

    /* trim all the other assorted crap */
    
    if (dwin->inbuf) {
        /* there's pending line input */
        dwin->infence -= cnum;
        dwin->incurs -= cnum;
    }
    
    if (dwin->scrollpos > cnum) {
        dwin->scrollpos -= cnum;
    }
    else {
        dwin->scrollpos = 0;
        dwin->drawall = TRUE;
    }
    
    if (dwin->scrollline > lnum) 
        dwin->scrollline -= lnum;
    else 
        dwin->scrollline = 0;

    if (dwin->lastseenline > lnum) 
        dwin->lastseenline -= lnum;
    else 
        dwin->lastseenline = 0;

    if (dwin->scrollline > dwin->numlines - dwin->height)
        dwin->scrollline = dwin->numlines - dwin->height;
    if (dwin->scrollline < 0)
        dwin->scrollline = 0;
}

void win_textbuffer_place_cursor(window_t *win, int *xpos, int *ypos)
{
    window_textbuffer_t *dwin = win->data;
    int ix;

    if (win->line_request) {
        /* figure out where the input cursor is. */
        long lx = find_line_by_pos(dwin, dwin->incurs);
        if (lx < 0 || lx - dwin->scrollline < 0) {
            *ypos = 0;
            *xpos = 0;
        }
        else if (lx - dwin->scrollline >= dwin->height) {
            *ypos = dwin->height - 1;
            *xpos = dwin->width - 1;
        }
        else {
            *ypos = lx - dwin->scrollline;
            ix = dwin->incurs - dwin->lines[lx].pos;
            if (ix >= dwin->width)
                ix = dwin->width-1;
            *xpos = ix;
        }
    }
    else {
        /* put the cursor at the end of the text. */
        long lx = dwin->numlines - 1;
        if (lx < 0 || lx - dwin->scrollline < 0) {
            *ypos = 0;
            *xpos = 0;
        }
        else if (lx - dwin->scrollline >= dwin->height) {
            *ypos = dwin->height - 1;
            *xpos = dwin->width - 1;
        }
        else {
            *ypos = lx - dwin->scrollline;
            ix = dwin->lines[lx].len;
            if (ix >= dwin->width)
                ix = dwin->width-1;
            *xpos = ix;
        }
    }
}

void win_textbuffer_set_paging(window_t *win, int forcetoend)
{
    window_textbuffer_t *dwin = win->data;
    int val;
    
    if (dwin->lastseenline == dwin->numlines)
        return;
    
    if (!forcetoend 
        && dwin->lastseenline - 0 < dwin->numlines - dwin->height) {
        /* scroll lastseenline to top, stick there */
        val = dwin->lastseenline - 1;
    }
    else {
        /* scroll to bottom, set lastseenline to end. */
        val = dwin->numlines - dwin->height;
        if (val < 0)
            val = 0;
        dwin->lastseenline = dwin->numlines;
    }

    if (val != dwin->scrollline) {
        dwin->scrollline = val;
        if (val >= dwin->numlines)
            dwin->scrollpos = dwin->numchars;
        else
            dwin->scrollpos = dwin->lines[val].pos;
        dwin->drawall = TRUE;
        updatetext(dwin);
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
    dwin->infence = dwin->numchars;
    dwin->incurs = dwin->numchars;
    dwin->inecho = win->echo_line_input;
    dwin->intermkeys = win->terminate_line_input;
    dwin->origstyle = win->style;
    win->style = style_Input;
    set_last_run(dwin, win->style);
    dwin->historypos = dwin->historypresent;
    
    if (initlen) {
        import_input_line(dwin, dwin->inbuf, dwin->inunicode, initlen);
    }

    if (gli_register_arr) {
        char *typedesc = (dwin->inunicode ? "&+#!Iu" : "&+#!Cn");
        dwin->inarrayrock = (*gli_register_arr)(dwin->inbuf, maxlen, typedesc);
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

    len = dwin->numchars - dwin->infence;
    if (inecho && win->echostr) 
        gli_stream_echo_line(win->echostr, &(dwin->chars[dwin->infence]), len);

    /* Store in event buffer. */
        
    if (len > inmax)
        len = inmax;
        
    export_input_line(inbuf, inunicode, len, &dwin->chars[dwin->infence]);
        
    if (!inecho) {
        /* Wipe the typed text from the buffer. */
        put_text(dwin, "", 0, dwin->infence, 
            dwin->numchars - dwin->infence);
    }
    
    win->style = dwin->origstyle;
    set_last_run(dwin, win->style);

    ev->type = evtype_LineInput;
    ev->win = win;
    ev->val1 = len;
    
    win->line_request = FALSE;
    dwin->inbuf = NULL;
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

static void import_input_line(window_textbuffer_t *dwin, void *buf, 
    int unicode, long len)
{
    /* len will be nonzero. */

    if (!unicode) {
        put_text(dwin, buf, len, dwin->incurs, 0);
    }
    else {
        int ix;
        char *cx = (char *)malloc(len * sizeof(char));
        for (ix=0; ix<len; ix++) {
            glui32 kval = ((glui32 *)buf)[ix];
            if (!(kval >= 0 && kval < 256))
                kval = '?';
            cx[ix] = kval;
        }
        put_text(dwin, cx, len, dwin->incurs, 0);
        free(cx);
    }
}

/* Clone in rgwin_grid.c */
/*### unicode argument?*/
static void export_input_line(void *buf, int unicode, long len, char *chars)
{
    int ix;

    if (!unicode) {
        for (ix=0; ix<len; ix++) {
            int val = chars[ix];
            glui32 kval = (val & 0xFF);
            if (!(kval >= 0 && kval < 256))
                kval = '?';
            ((unsigned char *)buf)[ix] = kval;
        }
    }
    else {
        for (ix=0; ix<len; ix++) {
            int val = chars[ix];
            glui32 kval = (val & 0xFF);
            ((glui32 *)buf)[ix] = kval;
        }
    }
}

