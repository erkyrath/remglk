/* gtwindow.c: Window objects
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "glk.h"
#include "remglk.h"
#include "gi_blorb.h"
#include "rgdata.h"
#include "rgwin_pair.h"
#include "rgwin_blank.h"
#include "rgwin_grid.h"
#include "rgwin_buf.h"
#include "rgwin_graph.h"

/* The update generation number. */
static glui32 generation = 0;

/* Used to generate window updatetag values. */
static glui32 tagcounter = 0;

/* Linked list of all windows */
static window_t *gli_windowlist = NULL; 

/* For use by gli_print_spaces() */
#define NUMSPACES (16)
static char spacebuffer[NUMSPACES+1];

window_t *gli_rootwin = NULL; /* The topmost window. */
window_t *gli_focuswin = NULL; /* The window selected by the player. 
    (This has nothing to do with the "current output stream", which is
    gli_currentstr in gtstream.c. In fact, the program doesn't know
    about gli_focuswin at all.) */

/* The current screen metrics. */
static data_metrics_t metrics;
/* Flag: Has the window arrangement changed at all? */
static int geometry_changed;

void (*gli_interrupt_handler)(void) = NULL;

static void compute_content_box(grect_t *box);

/* Set up the window system. This is called from main(). */
void gli_initialize_windows(data_metrics_t *newmetrics)
{
    int ix;

    generation = 0;
    srandom(time(NULL));
    tagcounter = (random() % 15) + 16;
    gli_rootwin = NULL;
    gli_focuswin = NULL;
    
    /* Build a convenient array of spaces. */
    for (ix=0; ix<NUMSPACES; ix++)
        spacebuffer[ix] = ' ';
    spacebuffer[NUMSPACES] = '\0';
    
    metrics = *newmetrics;

    geometry_changed = TRUE;
}

/* Get out fast. This is used by the ctrl-C interrupt handler, under Unix. 
    It doesn't pause and wait for a keypress, and it calls the Glk interrupt
    handler. Otherwise it's the same as glk_exit(). */
void gli_fast_exit()
{
    if (gli_interrupt_handler) {
        (*gli_interrupt_handler)();
    }

    gli_streams_close_all();
    exit(0);
}

static void compute_content_box(grect_t *box)
{
    box->left = metrics.outspacingx;
    box->top = metrics.outspacingy;
    box->right = metrics.width - metrics.outspacingx;
    box->bottom = metrics.height - metrics.outspacingy;
}

glui32 gli_window_current_generation()
{
    return generation;
}

window_t *gli_window_find_by_tag(glui32 tag)
{
    window_t *win;
    for (win=gli_windowlist; win; win=win->next) {
        if (win->updatetag == tag)
            return win;
    }
    return NULL;
}

window_t *gli_new_window(glui32 type, glui32 rock)
{
    window_t *win = (window_t *)malloc(sizeof(window_t));
    if (!win)
        return NULL;
    
    win->magicnum = MAGIC_WINDOW_NUM;
    win->rock = rock;
    win->type = type;
    win->updatetag = tagcounter;
    tagcounter += 3;
    
    win->parent = NULL; /* for now */
    win->data = NULL; /* for now */
    win->inputgen = 0;
    win->char_request = FALSE;
    win->line_request = FALSE;
    win->line_request_uni = FALSE;
    win->char_request_uni = FALSE;
    win->hyperlink_request = FALSE;
    win->echo_line_input = TRUE;
    win->terminate_line_input = 0;
    win->style = style_Normal;
    win->hyperlink = 0;

    win->str = gli_stream_open_window(win);
    win->echostr = NULL;

    win->prev = NULL;
    win->next = gli_windowlist;
    gli_windowlist = win;
    if (win->next) {
        win->next->prev = win;
    }
    
    if (gli_register_obj)
        win->disprock = (*gli_register_obj)(win, gidisp_Class_Window);
    
    return win;
}

void gli_delete_window(window_t *win)
{
    window_t *prev, *next;
    
    if (gli_unregister_obj)
        (*gli_unregister_obj)(win, gidisp_Class_Window, win->disprock);
        
    win->magicnum = 0;
    
    win->echostr = NULL;
    if (win->str) {
        gli_delete_stream(win->str);
        win->str = NULL;
    }
    
    prev = win->prev;
    next = win->next;
    win->prev = NULL;
    win->next = NULL;

    if (prev)
        prev->next = next;
    else
        gli_windowlist = next;
    if (next)
        next->prev = prev;
        
    free(win);
}

winid_t glk_window_open(winid_t splitwin, glui32 method, glui32 size, 
    glui32 wintype, glui32 rock)
{
    window_t *newwin, *pairwin, *oldparent;
    window_pair_t *dpairwin;
    grect_t box;
    glui32 val;
    
    if (!gli_rootwin) {
        if (splitwin) {
            gli_strict_warning("window_open: ref must be NULL");
            return 0;
        }
        /* ignore method and size now */
        oldparent = NULL;
        
        compute_content_box(&box);
    }
    else {
    
        if (!splitwin) {
            gli_strict_warning("window_open: ref must not be NULL");
            return 0;
        }
        
        val = (method & winmethod_DivisionMask);
        if (val != winmethod_Fixed && val != winmethod_Proportional) {
            gli_strict_warning("window_open: invalid method (not fixed or proportional)");
            return 0;
        }
        
        val = (method & winmethod_DirMask);
        if (val != winmethod_Above && val != winmethod_Below 
            && val != winmethod_Left && val != winmethod_Right) {
            gli_strict_warning("window_open: invalid method (bad direction)");
            return 0;
        }

        box = splitwin->bbox;
        
        oldparent = splitwin->parent;
        if (oldparent && oldparent->type != wintype_Pair) {
            gli_strict_warning("window_open: parent window is not Pair");
            return 0;
        }
    
    }

    if (wintype == wintype_Graphics && !pref_graphicswinsupport) {
        /* Graphics windows not supported; silently return null */
        return 0;
    }
    
    newwin = gli_new_window(wintype, rock);
    if (!newwin) {
        gli_strict_warning("window_open: unable to create window");
        return 0;
    }
    
    switch (wintype) {
        case wintype_Blank:
            newwin->data = win_blank_create(newwin);
            break;
        case wintype_TextGrid:
            newwin->data = win_textgrid_create(newwin);
            break;
        case wintype_TextBuffer:
            newwin->data = win_textbuffer_create(newwin);
            break;
        case wintype_Graphics:
            newwin->data = win_graphics_create(newwin);
            break;
        case wintype_Pair:
            gli_strict_warning("window_open: cannot open pair window directly");
            gli_delete_window(newwin);
            return 0;
        default:
            /* Unknown window type -- do not print a warning, just return 0
                to indicate that it's not possible. */
            gli_delete_window(newwin);
            return 0;
    }
    
    if (!newwin->data) {
        gli_strict_warning("window_open: unable to create window");
        return 0;
    }
    
    if (!splitwin) {
        gli_rootwin = newwin;
        gli_window_rearrange(newwin, &box, &metrics);
    }
    else {
        /* create pairwin, with newwin as the key */
        pairwin = gli_new_window(wintype_Pair, 0);
        dpairwin = win_pair_create(pairwin, method, newwin, size);
        pairwin->data = dpairwin;
            
        dpairwin->child1 = splitwin;
        dpairwin->child2 = newwin;
        
        splitwin->parent = pairwin;
        newwin->parent = pairwin;
        pairwin->parent = oldparent;

        if (oldparent) {
            window_pair_t *dparentwin = oldparent->data;
            if (dparentwin->child1 == splitwin)
                dparentwin->child1 = pairwin;
            else
                dparentwin->child2 = pairwin;
        }
        else {
            gli_rootwin = pairwin;
        }
        
        gli_window_rearrange(pairwin, &box, &metrics);
    }
    
    return newwin;
}

static void gli_window_close(window_t *win, int recurse)
{
    window_t *wx;
    
    if (gli_focuswin == win) {
        gli_focuswin = NULL;
    }
    
    for (wx=win->parent; wx; wx=wx->parent) {
        if (wx->type == wintype_Pair) {
            window_pair_t *dwx = wx->data;
            if (dwx->key == win) {
                dwx->key = NULL;
                dwx->keydamage = TRUE;
            }
        }
    }
    
    switch (win->type) {
        case wintype_Blank: {
            window_blank_t *dwin = win->data;
            win_blank_destroy(dwin);
            }
            break;
        case wintype_Pair: {
            window_pair_t *dwin = win->data;
            if (recurse) {
                if (dwin->child1)
                    gli_window_close(dwin->child1, TRUE);
                if (dwin->child2)
                    gli_window_close(dwin->child2, TRUE);
            }
            win_pair_destroy(dwin);
            }
            break;
        case wintype_TextBuffer: {
            window_textbuffer_t *dwin = win->data;
            win_textbuffer_destroy(dwin);
            }
            break;
        case wintype_TextGrid: {
            window_textgrid_t *dwin = win->data;
            win_textgrid_destroy(dwin);
            }
            break;
        case wintype_Graphics: {
            window_graphics_t *dwin = win->data;
            win_graphics_destroy(dwin);
            }
            break;
    }
    
    gli_delete_window(win);
}

void glk_window_close(window_t *win, stream_result_t *result)
{
    if (!win) {
        gli_strict_warning("window_close: invalid ref");
        return;
    }
        
    if (win == gli_rootwin || win->parent == NULL) {
        /* close the root window, which means all windows. */
        
        gli_rootwin = 0;
        
        /* begin (simpler) closation */
        geometry_changed = TRUE;
        
        gli_stream_fill_result(win->str, result);
        gli_window_close(win, TRUE); 
    }
    else {
        /* have to jigger parent */
        grect_t box;
        window_t *pairwin, *sibwin, *grandparwin, *wx;
        window_pair_t *dpairwin, *dgrandparwin;
        int keydamage_flag;
        
        pairwin = win->parent;
        dpairwin = pairwin->data;
        if (win == dpairwin->child1) {
            sibwin = dpairwin->child2;
        }
        else if (win == dpairwin->child2) {
            sibwin = dpairwin->child1;
        }
        else {
            gli_strict_warning("window_close: window tree is corrupted");
            return;
        }
        
        box = pairwin->bbox;

        grandparwin = pairwin->parent;
        if (!grandparwin) {
            gli_rootwin = sibwin;
            sibwin->parent = NULL;
        }
        else {
            dgrandparwin = grandparwin->data;
            if (dgrandparwin->child1 == pairwin)
                dgrandparwin->child1 = sibwin;
            else
                dgrandparwin->child2 = sibwin;
            sibwin->parent = grandparwin;
        }
        
        /* Begin closation */
        
        gli_stream_fill_result(win->str, result);

        /* Close the child window (and descendants), so that key-deletion can
            crawl up the tree to the root window. */
        gli_window_close(win, TRUE); 
        
        /* This probably isn't necessary, but the child *is* gone, so just
            in case. */
        if (win == dpairwin->child1) {
            dpairwin->child1 = NULL;
        }
        else if (win == dpairwin->child2) {
            dpairwin->child2 = NULL;
        }
        
        /* Now we can delete the parent pair. */
        gli_window_close(pairwin, FALSE);

        keydamage_flag = FALSE;
        for (wx=sibwin; wx; wx=wx->parent) {
            if (wx->type == wintype_Pair) {
                window_pair_t *dwx = wx->data;
                if (dwx->keydamage) {
                    keydamage_flag = TRUE;
                    dwx->keydamage = FALSE;
                }
            }
        }
        
        if (keydamage_flag) {
            compute_content_box(&box);
            gli_window_rearrange(gli_rootwin, &box, &metrics);
        }
        else {
            gli_window_rearrange(sibwin, &box, &metrics);
        }
    }
}

void glk_window_get_arrangement(window_t *win, glui32 *method, glui32 *size, 
    winid_t *keywin)
{
    window_pair_t *dwin;
    glui32 val;
    
    if (!win) {
        gli_strict_warning("window_get_arrangement: invalid ref");
        return;
    }
    
    if (win->type != wintype_Pair) {
        gli_strict_warning("window_get_arrangement: not a Pair window");
        return;
    }
    
    dwin = win->data;
    
    val = dwin->dir | dwin->division;
    if (!dwin->hasborder)
        val |= winmethod_NoBorder;
    
    if (size)
        *size = dwin->size;
    if (keywin) {
        if (dwin->key)
            *keywin = dwin->key;
        else
            *keywin = NULL;
    }
    if (method)
        *method = val;
}

void glk_window_set_arrangement(window_t *win, glui32 method, glui32 size, 
    winid_t key)
{
    window_pair_t *dwin;
    glui32 newdir;
    grect_t box;
    int newvertical, newbackward;
    
    if (!win) {
        gli_strict_warning("window_set_arrangement: invalid ref");
        return;
    }
    
    if (win->type != wintype_Pair) {
        gli_strict_warning("window_set_arrangement: not a Pair window");
        return;
    }
    
    if (key) {
        window_t *wx;
        if (key->type == wintype_Pair) {
            gli_strict_warning("window_set_arrangement: keywin cannot be a Pair");
            return;
        }
        for (wx=key; wx; wx=wx->parent) {
            if (wx == win)
                break;
        }
        if (wx == NULL) {
            gli_strict_warning("window_set_arrangement: keywin must be a descendant");
            return;
        }
    }
    
    dwin = win->data;
    box = win->bbox;
    
    newdir = method & winmethod_DirMask;
    newvertical = (newdir == winmethod_Left || newdir == winmethod_Right);
    newbackward = (newdir == winmethod_Left || newdir == winmethod_Above);
    if (!key)
        key = dwin->key;

    if ((newvertical && !dwin->vertical) || (!newvertical && dwin->vertical)) {
        if (!dwin->vertical)
            gli_strict_warning("window_set_arrangement: split must stay horizontal");
        else
            gli_strict_warning("window_set_arrangement: split must stay vertical");
        return;
    }
    
    if (key && key->type == wintype_Blank 
        && (method & winmethod_DivisionMask) == winmethod_Fixed) {
        gli_strict_warning("window_set_arrangement: a Blank window cannot have a fixed size");
        return;
    }

    if ((newbackward && !dwin->backward) || (!newbackward && dwin->backward)) {
        /* switch the children */
        window_t *tmpwin = dwin->child1;
        dwin->child1 = dwin->child2;
        dwin->child2 = tmpwin;
    }
    
    /* set up everything else */
    dwin->dir = newdir;
    dwin->division = method & winmethod_DivisionMask;
    dwin->key = key;
    dwin->size = size;
    dwin->hasborder = ((method & winmethod_BorderMask) == winmethod_Border);
    
    dwin->vertical = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Right);
    dwin->backward = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Above);
    
    gli_window_rearrange(win, &box, &metrics);
}

winid_t glk_window_iterate(winid_t win, glui32 *rock)
{
    if (!win) {
        win = gli_windowlist;
    }
    else {
        win = win->next;
    }
    
    if (win) {
        if (rock)
            *rock = win->rock;
        return win;
    }
    
    if (rock)
        *rock = 0;
    return NULL;
}

window_t *gli_window_iterate_treeorder(window_t *win)
{
    if (!win)
        return gli_rootwin;
    
    if (win->type == wintype_Pair) {
        window_pair_t *dwin = win->data;
        if (!dwin->backward)
            return dwin->child1;
        else
            return dwin->child2;
    }
    else {
        window_t *parwin;
        window_pair_t *dwin;
        
        while (win->parent) {
            parwin = win->parent;
            dwin = parwin->data;
            if (!dwin->backward) {
                if (win == dwin->child1)
                    return dwin->child2;
            }
            else {
                if (win == dwin->child2)
                    return dwin->child1;
            }
            win = parwin;
        }
        
        return NULL;
    }
}

glui32 glk_window_get_rock(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_get_rock: invalid ref.");
        return 0;
    }
    
    return win->rock;
}

winid_t glk_window_get_root()
{
    if (!gli_rootwin)
        return NULL;
    return gli_rootwin;
}

winid_t glk_window_get_parent(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_get_parent: invalid ref");
        return 0;
    }
    if (win->parent)
        return win->parent;
    else
        return 0;
}

winid_t glk_window_get_sibling(window_t *win)
{
    window_pair_t *dparwin;
    
    if (!win) {
        gli_strict_warning("window_get_sibling: invalid ref");
        return 0;
    }
    if (!win->parent)
        return 0;
    
    dparwin = win->parent->data;
    if (dparwin->child1 == win)
        return dparwin->child2;
    else if (dparwin->child2 == win)
        return dparwin->child1;
    return 0;
}

glui32 glk_window_get_type(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_get_parent: invalid ref");
        return 0;
    }
    return win->type;
}

void glk_window_get_size(window_t *win, glui32 *width, glui32 *height)
{
    glui32 wid = 0;
    glui32 hgt = 0;
    int val, boxwidth, boxheight;
    
    if (!win) {
        gli_strict_warning("window_get_size: invalid ref");
        return;
    }
    
    switch (win->type) {
        case wintype_Blank:
        case wintype_Pair:
            /* always zero */
            break;
        case wintype_TextGrid:
            boxwidth = win->bbox.right - win->bbox.left;
            boxheight = win->bbox.bottom - win->bbox.top;
            val = floor((boxwidth-metrics.gridmarginx) / metrics.gridcharwidth);
            wid = ((val >= 0) ? val : 0);
            val = floor((boxheight-metrics.gridmarginy) / metrics.gridcharheight);
            hgt = ((val >= 0) ? val : 0);
            break;
        case wintype_TextBuffer:
            boxwidth = win->bbox.right - win->bbox.left;
            boxheight = win->bbox.bottom - win->bbox.top;
            val = floor((boxwidth-metrics.buffermarginx) / metrics.buffercharwidth);
            wid = ((val >= 0) ? val : 0);
            val = floor((boxheight-metrics.buffermarginy) / metrics.buffercharheight);
            hgt = ((val >= 0) ? val : 0);
            break;
        case wintype_Graphics:
            boxwidth = win->bbox.right - win->bbox.left;
            boxheight = win->bbox.bottom - win->bbox.top;
            wid = boxwidth - metrics.graphicsmarginx;
            hgt = boxheight - metrics.graphicsmarginy;
            break;
    }

    if (width)
        *width = wid;
    if (height)
        *height = hgt;
}

strid_t glk_window_get_stream(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_get_stream: invalid ref");
        return NULL;
    }
    
    return win->str;
}

strid_t glk_window_get_echo_stream(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_get_echo_stream: invalid ref");
        return 0;
    }
    
    if (win->echostr)
        return win->echostr;
    else
        return 0;
}

void glk_window_set_echo_stream(window_t *win, stream_t *str)
{
    if (!win) {
        gli_strict_warning("window_set_echo_stream: invalid window id");
        return;
    }
    
    win->echostr = str;
}

void glk_set_window(window_t *win)
{
    if (!win) {
        gli_stream_set_current(NULL);
    }
    else {
        gli_stream_set_current(win->str);
    }
}

void gli_windows_unechostream(stream_t *str)
{
    window_t *win;
    
    for (win=gli_windowlist; win; win=win->next) {
        if (win->echostr == str)
            win->echostr = NULL;
    }
}

static glui32 *dup_buffer(void *buf, int len, int unicode)
{
    int ix;
    glui32 *res = malloc(len * sizeof(glui32));
    if (!res)
        return NULL;

    if (!unicode) {
        char *cbuf = (char *)buf;
        for (ix=0; ix<len; ix++)
            res[ix] = cbuf[ix];
    }
    else {
        glui32 *cbuf = (glui32 *)buf;
        for (ix=0; ix<len; ix++)
            res[ix] = cbuf[ix];
    }

    return res;
}

#if GIDEBUG_LIBRARY_SUPPORT

/* A cache of debug lines generated this cycle. */
static gen_list_t debug_output_cache = { NULL, 0, 0 };

void gidebug_output(char *text)
{
    /* Send a line of text to the "debug console", if the user has
       requested debugging mode. */
    if (gli_debugger) {
        gen_list_append(&debug_output_cache, strdup(text));
    }
}

#endif /* GIDEBUG_LIBRARY_SUPPORT */


/* This constructs an update object for the library state. (It's in
   rgwindow.c because most of the work is window-related.)

   This clears all the dirty flags, constructs an update, sends it to
   stdout, and flushes. 

   If special is provided, it goes into the update. It will be freed
   after sending.
*/
void gli_windows_update(data_specialreq_t *special, int newgeneration)
{
    window_t *win;
    int ix;
    data_update_t *update = data_update_alloc();

    if (newgeneration)
        generation++;
    update->gen = generation;

    if (geometry_changed) {
        geometry_changed = FALSE;

        update->usewindows = TRUE;

        for (win=gli_windowlist, ix=0; win; win=win->next, ix++) {
            if (win->type == wintype_Pair)
                continue;
            data_window_t *dat = data_window_alloc(win->updatetag,
                win->type, win->rock);
            dat->size = win->bbox;
            if (win->type == wintype_TextGrid) {
                window_textgrid_t *dwin = win->data;
                dat->gridwidth = dwin->width;
                dat->gridheight = dwin->height;
            }
            if (win->type == wintype_Graphics) {
                window_graphics_t *dwin = win->data;
                dat->gridwidth = dwin->graphwidth;
                dat->gridheight = dwin->graphheight;
            }
            gen_list_append(&update->windows, dat);
        }
    }
    
    for (win=gli_windowlist; win; win=win->next) {
        data_content_t *dat = NULL;
        switch (win->type) {
            case wintype_TextGrid:
                dat = win_textgrid_update(win);
                break;
            case wintype_TextBuffer:
                dat = win_textbuffer_update(win);
                break;
            case wintype_Graphics:
                dat = win_graphics_update(win);
                break;
        }

        if (dat) {
            gen_list_append(&update->contents, dat);
        }
    }

    update->useinputs = TRUE;
    for (win=gli_windowlist; win; win=win->next) {
        data_input_t *dat = NULL;
        if (win->char_request) {
            dat = data_input_alloc(win->updatetag, evtype_CharInput);
            dat->gen = win->inputgen;
            if (win->type == wintype_TextGrid) {
                window_textgrid_t *dwin = win->data;
                /* Canonicalize position first? */
                dat->cursorpos = TRUE;
                dat->xpos = dwin->curx;
                dat->ypos = dwin->cury;
            }
        }
        else if (win->line_request) {
            dat = data_input_alloc(win->updatetag, evtype_LineInput);
            dat->gen = win->inputgen;
            if (win->type == wintype_TextBuffer) {
                window_textbuffer_t *dwin = win->data;
                dat->maxlen = dwin->inmax;
                if (dwin->incurpos) {
                    dat->initlen = dwin->incurpos;
                    dat->initstr = dup_buffer(dwin->inbuf, dwin->incurpos, dwin->inunicode);
                }
            }
            else if (win->type == wintype_TextGrid) {
                window_textgrid_t *dwin = win->data;
                /* Canonicalize position first? */
                dat->cursorpos = TRUE;
                dat->xpos = dwin->curx;
                dat->ypos = dwin->cury;
                dat->maxlen = dwin->inmax;
                if (dwin->incurpos) {
                    dat->initlen = dwin->incurpos;
                    dat->initstr = dup_buffer(dwin->inbuf, dwin->incurpos, dwin->inunicode);
                }
            }
        }

        if (win->hyperlink_request) {
            if (!dat)
                dat = data_input_alloc(win->updatetag, evtype_None);
            dat->hyperlink = TRUE;
        }

        if (dat) {
            gen_list_append(&update->inputs, dat);
        }
    }

    glui32 timing_msec = 0;
    if (gli_timer_need_update(&timing_msec)) {
        update->includetimer = TRUE;
        update->timer = timing_msec;
    }

    update->specialreq = special;

#if GIDEBUG_LIBRARY_SUPPORT
    for (ix=0; ix<debug_output_cache.count; ix++) {
        gen_list_append(&update->debuglines, debug_output_cache.list[ix]);
        debug_output_cache.list[ix] = NULL;
    }
    debug_output_cache.count = 0;
#endif /* GIDEBUG_LIBRARY_SUPPORT */

    data_update_print(update);
    printf("\n"); /* blank line after stanza */
    fflush(stdout);

    data_update_free(update);
}

/* Set dirty flags on everything, as if the client hasn't seen any
   updates since the given generation number.

   ### This ignores the generation number and just resends everything
   we've got.
*/
void gli_windows_refresh(glui32 fromgen)
{
    window_t *win;
    for (win=gli_windowlist; win; win=win->next) {
        if (win->type == wintype_TextBuffer) {
            window_textbuffer_t *dwin = win->data;
            dwin->updatemark = 0;
        }
        else if (win->type == wintype_TextGrid) {
            window_textgrid_t *dwin = win->data;
            dwin->alldirty = TRUE;
        }
        else if (win->type == wintype_Graphics) {
            window_graphics_t *dwin = win->data;
            dwin->updatemark = 0;
        }
    }
}

/* Some trivial switch functions which make up for the fact that we're not
    doing this in C++. */

void gli_window_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics)
{
    geometry_changed = TRUE;

    switch (win->type) {
        case wintype_Blank:
            win_blank_rearrange(win, box, metrics);
            break;
        case wintype_Pair:
            win_pair_rearrange(win, box, metrics);
            break;
        case wintype_TextGrid:
            win_textgrid_rearrange(win, box, metrics);
            break;
        case wintype_TextBuffer:
            win_textbuffer_rearrange(win, box, metrics);
            break;
        case wintype_Graphics:
            win_graphics_rearrange(win, box, metrics);
            break;
    }
}

void gli_window_prepare_input(window_t *win, glui32 *buf, glui32 len)
{
    switch (win->type) {
        case wintype_TextGrid:
            win_textgrid_prepare_input(win, buf, len);
            break;
        case wintype_TextBuffer:
            win_textbuffer_prepare_input(win, buf, len);
            break;
    }
}

void gli_window_accept_line(window_t *win)
{
    switch (win->type) {
        case wintype_TextGrid:
            win_textgrid_accept_line(win);
            break;
        case wintype_TextBuffer:
            win_textbuffer_accept_line(win);
            break;
    }
}

void gli_windows_metrics_change(data_metrics_t *newmetrics)
{
    metrics = *newmetrics;

    if (gli_rootwin) {
        grect_t box;
        compute_content_box(&box);
        gli_window_rearrange(gli_rootwin, &box, &metrics);
    }
    
    gli_event_store(evtype_Arrange, NULL, 0, 0);
}

void gli_windows_trim_buffers()
{
    window_t *win;
    
    for (win=gli_windowlist; win; win=win->next) {
        switch (win->type) {
            case wintype_TextBuffer:
                win_textbuffer_trim_buffer(win);
                break;
            case wintype_Graphics:
                win_graphics_trim_buffer(win);
                break;
        }
    }
}

void glk_request_char_event(window_t *win)
{
    if (!win) {
        gli_strict_warning("request_char_event: invalid ref");
        return;
    }
    
    if (win->char_request || win->line_request) {
        gli_strict_warning("request_char_event: window already has keyboard request");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
        case wintype_TextGrid:
            win->char_request = TRUE;
            win->char_request_uni = FALSE;
            win->inputgen = generation+1;
            break;
        default:
            gli_strict_warning("request_char_event: window does not support keyboard input");
            break;
    }
    
}

void glk_request_line_event(window_t *win, char *buf, glui32 maxlen, 
    glui32 initlen)
{
    if (!win) {
        gli_strict_warning("request_line_event: invalid ref");
        return;
    }
    
    if (win->char_request || win->line_request) {
        gli_strict_warning("request_line_event: window already has keyboard request");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
            win->line_request = TRUE;
            win->line_request_uni = FALSE;
            win->inputgen = generation+1;
            win_textbuffer_init_line(win, buf, FALSE, maxlen, initlen);
            break;
        case wintype_TextGrid:
            win->line_request = TRUE;
            win->line_request_uni = FALSE;
            win->inputgen = generation+1;
            win_textgrid_init_line(win, buf, FALSE, maxlen, initlen);
            break;
        default:
            gli_strict_warning("request_line_event: window does not support keyboard input");
            break;
    }
    
}

#ifdef GLK_MODULE_UNICODE

void glk_request_char_event_uni(window_t *win)
{
    if (!win) {
        gli_strict_warning("request_char_event: invalid ref");
        return;
    }
    
    if (win->char_request || win->line_request) {
        gli_strict_warning("request_char_event: window already has keyboard request");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
        case wintype_TextGrid:
            win->char_request = TRUE;
            win->char_request_uni = TRUE;
            win->inputgen = generation+1;
            break;
        default:
            gli_strict_warning("request_char_event: window does not support keyboard input");
            break;
    }
    
}

void glk_request_line_event_uni(window_t *win, glui32 *buf, glui32 maxlen, 
    glui32 initlen)
{
    if (!win) {
        gli_strict_warning("request_line_event: invalid ref");
        return;
    }
    
    if (win->char_request || win->line_request) {
        gli_strict_warning("request_line_event: window already has keyboard request");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
            win->line_request = TRUE;
            win->line_request_uni = TRUE;
            win->inputgen = generation+1;
            win_textbuffer_init_line(win, buf, TRUE, maxlen, initlen);
            break;
        case wintype_TextGrid:
            win->line_request = TRUE;
            win->line_request_uni = TRUE;
            win->inputgen = generation+1;
            win_textgrid_init_line(win, buf, TRUE, maxlen, initlen);
            break;
        default:
            gli_strict_warning("request_line_event: window does not support keyboard input");
            break;
    }
    
}

#endif /* GLK_MODULE_UNICODE */

void glk_request_mouse_event(window_t *win)
{
    if (!win) {
        gli_strict_warning("request_mouse_event: invalid ref");
        return;
    }
    
    /* But, in fact, we can't do much about this. */
    
    return;
}

void glk_cancel_char_event(window_t *win)
{
    if (!win) {
        gli_strict_warning("cancel_char_event: invalid ref");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
        case wintype_TextGrid:
            win->char_request = FALSE;
            win->inputgen = 0;
            break;
        default:
            /* do nothing */
            break;
    }
}

void glk_cancel_line_event(window_t *win, event_t *ev)
{
    event_t dummyev;
    
    if (!ev) {
        ev = &dummyev;
    }

    gli_event_clearevent(ev);
    
    if (!win) {
        gli_strict_warning("cancel_line_event: invalid ref");
        return;
    }
    
    switch (win->type) {
        case wintype_TextBuffer:
            if (win->line_request) {
                win_textbuffer_cancel_line(win, ev);
                win->inputgen = 0;
            }
            break;
        case wintype_TextGrid:
            if (win->line_request) {
                win_textgrid_cancel_line(win, ev);
                win->inputgen = 0;
            }
            break;
        default:
            /* do nothing */
            break;
    }
}

void glk_cancel_mouse_event(window_t *win)
{
    if (!win) {
        gli_strict_warning("cancel_mouse_event: invalid ref");
        return;
    }
    
    /* But, in fact, we can't do much about this. */
    
    return;
}

void gli_window_put_char(window_t *win, glui32 ch)
{
    switch (win->type) {
        case wintype_TextBuffer:
            win_textbuffer_putchar(win, ch);
            break;
        case wintype_TextGrid:
            win_textgrid_putchar(win, ch);
            break;
    }
}

void glk_window_clear(window_t *win)
{
    if (!win) {
        gli_strict_warning("window_clear: invalid ref");
        return;
    }
    
    if (win->line_request) {
        gli_strict_warning("window_clear: window has pending line request");
        return;
    }

    switch (win->type) {
        case wintype_TextBuffer:
            win_textbuffer_clear(win);
            break;
        case wintype_TextGrid:
            win_textgrid_clear(win);
            break;
        case wintype_Graphics:
            win_graphics_clear(win);
            break;
    }
}

void glk_window_move_cursor(window_t *win, glui32 xpos, glui32 ypos)
{
    if (!win) {
        gli_strict_warning("window_move_cursor: invalid ref");
        return;
    }
    
    switch (win->type) {
        case wintype_TextGrid:
            win_textgrid_move_cursor(win, xpos, ypos);
            break;
        default:
            gli_strict_warning("window_move_cursor: not a TextGrid window");
            break;
    }
}

#ifdef GLK_MODULE_LINE_ECHO

void glk_set_echo_line_event(window_t *win, glui32 val)
{
    if (!win) {
        gli_strict_warning("set_echo_line_event: invalid ref");
        return;
    }
    
    win->echo_line_input = (val != 0);
}

#endif /* GLK_MODULE_LINE_ECHO */

#ifdef GLK_MODULE_LINE_TERMINATORS

void glk_set_terminators_line_event(window_t *win, glui32 *keycodes, 
    glui32 count)
{
    int ix;
    glui32 res, val;

    if (!win) {
        gli_strict_warning("set_terminators_line_event: invalid ref");
        return;
    }
    
    /* We only allow escape and the function keys as line input terminators.
       We encode those in a bitmask. */
    res = 0;
    if (keycodes) {
        for (ix=0; ix<count; ix++) {
            if (keycodes[ix] == keycode_Escape) {
                res |= 0x10000;
            }
            else {
                val = keycode_Func1 + 1 - keycodes[ix];
                if (val >= 1 && val <= 12)
                    res |= (1 << val);
            }
        }
    }

    win->terminate_line_input = res;
}

#endif /* GLK_MODULE_LINE_TERMINATORS */

#ifdef GLK_MODULE_IMAGE

glui32 glk_image_draw(winid_t win, glui32 image, glsi32 val1, glsi32 val2)
{
    if (!pref_graphicssupport) {
        gli_strict_warning("image_draw: graphics not supported.");
        return FALSE;
    }

    giblorb_map_t *map = giblorb_get_resource_map();
    if (!map)
        return FALSE; /* Not running from a blorb file */

    giblorb_image_info_t info;
    giblorb_err_t err = giblorb_load_image_info(map, image, &info);
    if (err)
        return FALSE;

    if (win->type == wintype_TextBuffer) {
        data_specialspan_t *special = data_specialspan_alloc(specialtype_Image);
        special->image = image;
        special->chunktype = info.chunktype;
        special->width = info.width;
        special->height = info.height;
        special->alttext = info.alttext;
        special->alignment = val1;
        special->hyperlink = win->hyperlink;
        win_textbuffer_putspecial(win, special);
        return TRUE;
    }
    else if (win->type == wintype_Graphics) {
        data_specialspan_t *special = data_specialspan_alloc(specialtype_Image);
        special->image = image;
        special->chunktype = info.chunktype;
        special->width = info.width;
        special->height = info.height;
        special->xpos = val1;
        special->ypos = val2;
        special->alttext = info.alttext;
        win_graphics_putspecial(win, special);
        return TRUE;
    }

    return FALSE;
}

glui32 glk_image_draw_scaled(winid_t win, glui32 image, 
    glsi32 val1, glsi32 val2, glui32 width, glui32 height)
{
    if (!pref_graphicssupport) {
        gli_strict_warning("image_draw_scaled: graphics not supported.");
        return FALSE;
    }

    /* Same as above, except we use the passed-in width and height values */

    giblorb_map_t *map = giblorb_get_resource_map();
    if (!map)
        return FALSE; /* Not running from a blorb file */

    giblorb_image_info_t info;
    giblorb_err_t err = giblorb_load_image_info(map, image, &info);
    if (err)
        return FALSE;

    if (win->type == wintype_TextBuffer) {
        data_specialspan_t *special = data_specialspan_alloc(specialtype_Image);
        special->image = image;
        special->chunktype = info.chunktype;
        special->width = width;
        special->height = height;
        special->alttext = info.alttext;
        special->alignment = val1;
        special->hyperlink = win->hyperlink;
        win_textbuffer_putspecial(win, special);
        return TRUE;
    }
    else if (win->type == wintype_Graphics) {
        data_specialspan_t *special = data_specialspan_alloc(specialtype_Image);
        special->image = image;
        special->chunktype = info.chunktype;
        special->width = width;
        special->height = height;
        special->xpos = val1;
        special->ypos = val2;
        special->alttext = info.alttext;
        win_graphics_putspecial(win, special);
        return TRUE;
    }

    return FALSE;
}

glui32 glk_image_get_info(glui32 image, glui32 *width, glui32 *height)
{
    if (width)
        *width = 0;
    if (height)
        *height = 0;

    giblorb_map_t *map = giblorb_get_resource_map();
    if (!map)
        return FALSE; /* Not running from a blorb file */

    giblorb_image_info_t info;
    giblorb_err_t err = giblorb_load_image_info(map, image, &info);
    if (err)
        return FALSE;

    if (width)
        *width = info.width;
    if (height)
        *height = info.height;

    return TRUE;
}

void glk_window_flow_break(winid_t win)
{
    if (!win) {
        gli_strict_warning("flow_break: invalid ref");
        return;
    }
    
    if (win->type == wintype_TextBuffer) {
        data_specialspan_t *special = data_specialspan_alloc(specialtype_FlowBreak);
        win_textbuffer_putspecial(win, special);
    }
}

void glk_window_erase_rect(winid_t win, 
    glsi32 left, glsi32 top, glui32 width, glui32 height)
{
    if (!win) {
        gli_strict_warning("window_erase_rect: invalid ref");
        return;
    }
    if (win->type != wintype_Graphics) {
        gli_strict_warning("window_erase_rect: not a graphics window");
        return;
    }

    data_specialspan_t *special = data_specialspan_alloc(specialtype_Fill);
    special->hasdimensions = TRUE;
    special->xpos = left;
    special->ypos = top;
    special->width = width;
    special->height = height;
    win_graphics_putspecial(win, special);
}

void glk_window_fill_rect(winid_t win, glui32 color, 
    glsi32 left, glsi32 top, glui32 width, glui32 height)
{
    if (!win) {
        gli_strict_warning("window_fill_rect: invalid ref");
        return;
    }
    if (win->type != wintype_Graphics) {
        gli_strict_warning("window_fill_rect: not a graphics window");
        return;
    }

    data_specialspan_t *special = data_specialspan_alloc(specialtype_Fill);
    special->hasdimensions = TRUE;
    special->xpos = left;
    special->ypos = top;
    special->width = width;
    special->height = height;
    special->hascolor = TRUE;
    special->color = color;
    win_graphics_putspecial(win, special);
}

void glk_window_set_background_color(winid_t win, glui32 color)
{
    if (!win) {
        gli_strict_warning("window_set_background_color: invalid ref");
        return;
    }
    if (win->type != wintype_Graphics) {
        gli_strict_warning("window_set_background_color: not a graphics window");
        return;
    }

    data_specialspan_t *special = data_specialspan_alloc(specialtype_SetColor);
    special->hascolor = TRUE;
    special->color = color;
    win_graphics_putspecial(win, special);
}

#endif /* GLK_MODULE_IMAGE */

#ifdef GLK_MODULE_HYPERLINKS

void glk_request_hyperlink_event(winid_t win)
{
    if (!win) {
        gli_strict_warning("request_hyperlink_event: invalid ref");
        return;
    }

    if (!pref_hyperlinksupport)
        return;

    switch (win->type) {
        case wintype_TextBuffer:
        case wintype_TextGrid:
            win->hyperlink_request = TRUE;
            break;
        default:
            gli_strict_warning("request_hyperlink_event: window does not support hyperlink input");
            break;
    }
}

void glk_cancel_hyperlink_event(winid_t win)
{
    if (!win) {
        gli_strict_warning("cancel_hyperlink_event: invalid ref");
        return;
    }

    if (!pref_hyperlinksupport)
        return;

    switch (win->type) {
        case wintype_TextBuffer:
        case wintype_TextGrid:
            win->hyperlink_request = FALSE;
            break;
        default:
            gli_strict_warning("cancel_hyperlink_event: window does not support hyperlink input");
            break;
    }
}

#endif /* GLK_MODULE_HYPERLINKS */
