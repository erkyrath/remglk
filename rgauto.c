/* rgauto.c: Support for autosave/autorestore
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>

#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "rgdata_int.h"
#include "rgwin_pair.h"
#include "rgwin_buf.h"
#include "rgwin_grid.h"
#include "rgwin_graph.h"

#define SERIAL_VERSION (1)

static void window_state_print(FILE *fl, winid_t win);
static void stream_state_print(FILE *fl, strid_t str);
static void fileref_state_print(FILE *fl, frefid_t fref);
static void tbrun_print(FILE *fl, tbrun_t *run);
static void tgline_print(FILE *fl, tgline_t *line, int width);

void glkunix_save_library_state(strid_t file, glkunix_serialize_object_f extra_state_func, void *extra_state_rock)
{
    FILE *fl = file->file;
    winid_t tmpwin;
    strid_t tmpstr;
    frefid_t tmpfref;
    int first;
    
    fprintf(fl, "{\"type\":\"autosave\", \"version\":%d", SERIAL_VERSION);

    fprintf(fl, ",\n\"metrics\":");
    data_metrics_print(fl, gli_windows_get_metrics());

    /* We don't use data_window_print (etc) here because we need a complete state dump for the autosave. It's way beyond the documented RemGlk/GlkOte JSON API. */
    
    fprintf(fl, ",\n\"windows\": [\n");
    first = TRUE;
    for (tmpwin = glk_window_iterate(NULL, NULL); tmpwin; tmpwin = glk_window_iterate(tmpwin, NULL)) {
        if (!first) fprintf(fl, ",\n");
        first = FALSE;
        window_state_print(fl, tmpwin);
    }
    fprintf(fl, "]");
    
    fprintf(fl, ",\n\"streams\": [\n");
    first = TRUE;
    for (tmpstr = glk_stream_iterate(NULL, NULL); tmpstr; tmpstr = glk_stream_iterate(tmpstr, NULL)) {
        if (!first) fprintf(fl, ",\n");
        first = FALSE;
        stream_state_print(fl, tmpstr);
    }
    fprintf(fl, "]");
    
    fprintf(fl, ",\n\"filerefs\": [\n");
    first = TRUE;
    for (tmpfref = glk_fileref_iterate(NULL, NULL); tmpfref; tmpfref = glk_fileref_iterate(tmpfref, NULL)) {
        if (!first) fprintf(fl, ",\n");
        first = FALSE;
        fileref_state_print(fl, tmpfref);
    }
    fprintf(fl, "]");
    
    glui32 timerinterval = gli_timer_get_timing_msec();
    if (timerinterval) {
        fprintf(fl, ",\n\"timerinterval\":%ld", (long)timerinterval);
    }

    if (gli_rootwin) {
        fprintf(fl, ",\n\"rootwintag\":%ld", (long)gli_rootwin->updatetag);
    }
    if (gli_currentstr) {
        fprintf(fl, ",\n\"currentstrtag\":%ld", (long)gli_currentstr->updatetag);
    }

    if (extra_state_func) {
        struct glkunix_serialize_context_struct ctx;
        fprintf(fl, ",\n\"extra_state\":");
        glkunix_serialize_object_root(fl, &ctx, extra_state_func, extra_state_rock);
    }
    
    fprintf(fl, "}\n");
}

static void window_state_print(FILE *fl, winid_t win)
{
    int first;
    int ix;
    
    fprintf(fl, "{\"tag\":\"%ld\"", (long)win->updatetag);
    fprintf(fl, ",\n\"type\":%ld, \"rock\":%ld", (long)win->type, (long)win->rock);
    /* disprock is handled elsewhere */

    fprintf(fl, ",\n\"bbox\":");
    data_grect_print(fl, &win->bbox);

    if (win->parent)
        fprintf(fl, ",\n\"parenttag\":%ld", (long)win->parent->updatetag);

    if (win->str)
        fprintf(fl, ",\n\"streamtag\":%ld", (long)win->str->updatetag);
    if (win->echostr)
        fprintf(fl, ",\n\"echostreamtag\":%ld", (long)win->echostr->updatetag);

    fprintf(fl, ",\n\"inputgen\":%ld", (long)win->inputgen);
    fprintf(fl, ",\n\"line_request\":%d", win->line_request);
    fprintf(fl, ",\n\"line_request_uni\":%d", win->line_request_uni);
    fprintf(fl, ",\n\"char_request\":%d", win->char_request);
    fprintf(fl, ",\n\"char_request_uni\":%d", win->char_request_uni);
    fprintf(fl, ",\n\"hyperlink_request\":%d", win->hyperlink_request);

    /* The input buffer is handled below */

    fprintf(fl, ",\n\"echo_line_input\":%d", win->echo_line_input);
    fprintf(fl, ",\n\"terminate_line_input\":%ld", (long)win->terminate_line_input);
    
    fprintf(fl, ",\n\"style\":%ld", (long)win->style);
    fprintf(fl, ",\n\"hyperlink\":%ld", (long)win->hyperlink);

    switch (win->type) {
        
    case wintype_Pair: {
        window_pair_t *dwin = win->data;
        if (dwin->child1)
            fprintf(fl, ",\n\"pair_child1tag\":%ld", (long)dwin->child1->updatetag);
        if (dwin->child2)
            fprintf(fl, ",\n\"pair_child2tag\":%ld", (long)dwin->child2->updatetag);

        fprintf(fl, ",\n\"pair_splitpos\":%d", dwin->splitpos);
        fprintf(fl, ",\n\"pair_splitwidth\":%d", dwin->splitwidth);

        fprintf(fl, ",\n\"pair_dir\":%ld", (long)dwin->dir);
        fprintf(fl, ",\n\"pair_vertical\":%d", dwin->vertical);
        fprintf(fl, ",\n\"pair_backward\":%d", dwin->backward);
        fprintf(fl, ",\n\"pair_hasborder\":%d", dwin->hasborder);
        fprintf(fl, ",\n\"pair_division\":%ld", (long)dwin->division);
        if (dwin->key)
             fprintf(fl, ",\n\"pair_keytag\":%ld", (long)dwin->key->updatetag);
        /* keydamage is temporary */
        fprintf(fl, ",\n\"pair_size\":%ld", (long)dwin->size);
        
        break;
    }
        
    case wintype_TextBuffer: {
        window_textbuffer_t *dwin = win->data;
        fprintf(fl, ",\n\"buf_width\":%d, \"buf_height\":%d", dwin->width, dwin->height);
        //### think about this
        fprintf(fl, ",\n\"buf_updatemark\":%ld", (long)dwin->updatemark);
        fprintf(fl, ",\n\"buf_startclear\":%d", dwin->startclear);
        
        fprintf(fl, ",\n\"buf_runs\":[\n");
        first = TRUE;
        for (ix=0; ix<dwin->numruns; ix++) {
            if (!first) fprintf(fl, ",\n");
            first = FALSE;
            tbrun_print(fl, &dwin->runs[ix]);
        }
        fprintf(fl, "]");

        fprintf(fl, ",\n\"buf_specials\":[\n");
        first = TRUE;
        for (ix=0; ix<dwin->numspecials; ix++) {
            if (!first) fprintf(fl, ",\n");
            first = FALSE;
            data_specialspan_auto_print(fl, dwin->specials[ix]);
        }
        fprintf(fl, "]");

        fprintf(fl, ",\n\"buf_chars\":\n");
        print_ustring_len_json(dwin->chars, dwin->numchars, fl);

        /* Fields only relevant during line input. */
        if (dwin->inbuf && dwin->inmax && gli_dispatch_locate_arr) {
            if (dwin->incurpos)
                fprintf(fl, ",\n\"buf_incurpos\":%ld", (long)dwin->incurpos);
            fprintf(fl, ",\n\"buf_inunicode\":%d", dwin->inunicode);
            if (dwin->inecho)
                fprintf(fl, ",\n\"buf_inecho\":%d", dwin->inecho);
            if (dwin->intermkeys)
                fprintf(fl, ",\n\"buf_intermkeys\":%ld", (long)dwin->intermkeys);
            fprintf(fl, ",\n\"buf_inmax\":%d", dwin->inmax);
            if (dwin->origstyle)
                fprintf(fl, ",\n\"buf_origstyle\":%ld", (long)dwin->origstyle);
            if (dwin->orighyperlink)
                fprintf(fl, ",\n\"buf_orighyperlink\":%ld", (long)dwin->orighyperlink);

            long bufaddr;
            int elemsize;
            int len;
            if (!dwin->inunicode) {
                bufaddr = (*gli_dispatch_locate_arr)(dwin->inbuf, dwin->inmax, "&+#!Cn", dwin->inarrayrock, &elemsize);
                fprintf(fl, ",\n\"buf_line_buffer\":%ld", bufaddr);
                if (elemsize) {
                    char *inbuf = dwin->inbuf;
                    if (elemsize != 1)
                        gli_fatal_error("bufwin encoding char array: wrong elemsize");
                    for (len=dwin->inmax; len > 0 && !inbuf[len-1]; len--) {}
                    fprintf(fl, ",\n\"buf_line_buffer_data\":");
                    print_string_len_json(inbuf, len, fl);
                }
            }
            else {
                bufaddr = (*gli_dispatch_locate_arr)(dwin->inbuf, dwin->inmax, "&+#!Iu", dwin->inarrayrock, &elemsize);
                fprintf(fl, ",\n\"buf_line_buffer\":%ld", bufaddr);
                if (elemsize) {
                    glui32 *inbuf = dwin->inbuf;
                    if (elemsize != 4)
                        gli_fatal_error("bufwin encoding uni array: wrong elemsize");
                    for (len=dwin->inmax; len > 0 && !inbuf[len-1]; len--) {}
                    fprintf(fl, ",\n\"buf_line_buffer_data\":");
                    print_ustring_len_json(inbuf, len, fl);
                }
            }
        }
        
        break;
    }

    case wintype_TextGrid: {
        window_textgrid_t *dwin = win->data;
        fprintf(fl, ",\n\"grid_width\":%d, \"grid_height\":%d", dwin->width, dwin->height);
        fprintf(fl, ",\n\"grid_curx\":%d, \"grid_cury\":%d", dwin->curx, dwin->cury);

        /* nothing is dirty at autosave (select) time */

        fprintf(fl, ",\n\"grid_lines\":[\n");
        first = TRUE;
        for (ix=0; ix<dwin->height; ix++) {
            if (!first) fprintf(fl, ",\n");
            first = FALSE;
            tgline_print(fl, &dwin->lines[ix], dwin->width);
        }
        fprintf(fl, "]");
        

        /* Fields only relevant during line input. */
        if (dwin->inbuf && dwin->inoriglen && gli_dispatch_locate_arr) {
            if (dwin->incurpos)
                fprintf(fl, ",\n\"grid_incurpos\":%ld", (long)dwin->incurpos);
            fprintf(fl, ",\n\"grid_inunicode\":%d", dwin->inunicode);
            if (dwin->inecho)
                fprintf(fl, ",\n\"grid_inecho\":%d", dwin->inecho);
            if (dwin->intermkeys)
                fprintf(fl, ",\n\"grid_intermkeys\":%ld", (long)dwin->intermkeys);
            fprintf(fl, ",\n\"grid_inmax\":%d", dwin->inmax);
            fprintf(fl, ",\n\"grid_inoriglen\":%d", dwin->inoriglen);
            if (dwin->origstyle)
                fprintf(fl, ",\n\"grid_origstyle\":%ld", (long)dwin->origstyle);
            /*
            if (dwin->orighyperlink)
                fprintf(fl, ",\n\"grid_orighyperlink\":%ld", (long)dwin->orighyperlink);
            */

            long bufaddr;
            int elemsize;
            int len;
            if (!dwin->inunicode) {
                bufaddr = (*gli_dispatch_locate_arr)(dwin->inbuf, dwin->inoriglen, "&+#!Cn", dwin->inarrayrock, &elemsize);
                fprintf(fl, ",\n\"grid_line_buffer\":%ld", bufaddr);
                if (elemsize) {
                    char *inbuf = dwin->inbuf;
                    if (elemsize != 1)
                        gli_fatal_error("gridwin encoding char array: wrong elemsize");
                    for (len=dwin->inoriglen; len > 0 && !inbuf[len-1]; len--) {}
                    fprintf(fl, ",\n\"grid_line_buffer_data\":");
                    print_string_len_json(inbuf, len, fl);
                }
            }
            else {
                bufaddr = (*gli_dispatch_locate_arr)(dwin->inbuf, dwin->inoriglen, "&+#!Iu", dwin->inarrayrock, &elemsize);
                fprintf(fl, ",\n\"grid_line_buffer\":%ld", bufaddr);
                if (elemsize) {
                    glui32 *inbuf = dwin->inbuf;
                    if (elemsize != 4)
                        gli_fatal_error("gridwin encoding uni array: wrong elemsize");
                    for (len=dwin->inoriglen; len > 0 && !inbuf[len-1]; len--) {}
                    fprintf(fl, ",\n\"grid_line_buffer_data\":");
                    print_ustring_len_json(inbuf, len, fl);
                }
            }
        }
        
        break;
    }

    case wintype_Graphics: {
        window_graphics_t *dwin = win->data;
        fprintf(fl, ",\n\"graph_width\":%d, \"graph_height\":%d", dwin->graphwidth, dwin->graphheight);

        //### think about this
        fprintf(fl, ",\n\"graph_updatemark\":%ld", (long)dwin->updatemark);

        fprintf(fl, ",\n\"graph_content\":[\n");
        first = TRUE;
        for (ix=0; ix<dwin->numcontent; ix++) {
            if (!first) fprintf(fl, ",\n");
            first = FALSE;
            data_specialspan_auto_print(fl, dwin->content[ix]);
        }
        fprintf(fl, "]");
        
        break;
    }

    }

    fprintf(fl, "}\n");
}

static void tbrun_print(FILE *fl, tbrun_t *run)
{
    fprintf(fl, "{\"style\":%d", run->style);
    if (run->hyperlink)
        fprintf(fl, ", \"hyperlink\":%ld", (long)run->hyperlink);
    fprintf(fl, ", \"pos\":%ld", run->pos);
    if (run->specialnum != -1)
        fprintf(fl, ", \"specialnum\":%ld", run->specialnum);
    fprintf(fl, "}\n");
}

static void tgline_print(FILE *fl, tgline_t *line, int width)
{
    int ix;
    int len;

    /* We omit trailing spaces in the chars array. */
    
    for (len=width; len > 0; len--) {
        if (line->chars[len-1] != ' ') break;
    }
    
    fprintf(fl, "{\"chars\":");
    print_ustring_len_json(line->chars, len, fl);

    /* We omit trailing zeroes in the styles and links arrays. If the array is all-zero, we omit the whole thing. */

    for (len=width; len > 0; len--) {
        if (line->styles[len-1]) break;
    }

    if (len) {
        fprintf(fl, ",\n\"styles\":[");
        for (ix=0; ix<len; ix++) {
            if (ix) fprintf(fl, ",");
            fprintf(fl, "%d", (int)line->styles[ix]);
        }
        fprintf(fl, "]");
    }

    for (len=width; len > 0; len--) {
        if (line->links[len-1]) break;
    }

    if (len) {
        fprintf(fl, ",\n\"links\":[");
        for (ix=0; ix<len; ix++) {
            if (ix) fprintf(fl, ",");
            fprintf(fl, "%ld", (long)line->links[ix]);
        }
        fprintf(fl, "]");
    }
    
    fprintf(fl, "}\n");
}

static void stream_state_print(FILE *fl, strid_t str)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)str->updatetag);
    fprintf(fl, ",\n\"type\":%d, \"rock\":%ld", str->type, (long)str->rock);
    /* disprock is handled elsewhere */

    fprintf(fl, ", \"unicode\":%d", str->unicode);

    fprintf(fl, ", \"readable\":%d", str->readable);
    fprintf(fl, ", \"writable\":%d", str->writable);

    fprintf(fl, ", \"readcount\":%ld", (long)str->readcount);
    fprintf(fl, ", \"writecount\":%ld", (long)str->writecount);

    switch (str->type) {

    case strtype_Window: {
        if (str->win)
            fprintf(fl, ", \"win_tag\":%ld", (long)str->win->updatetag);
        break;
    }

    case strtype_File: {
        if (str->isbinary)
            fprintf(fl, ", \"file_isbinary\":%d", str->isbinary);
        if (str->lastop)
            fprintf(fl, ", \"file_lastop\":%ld", (long)str->lastop);
        //### file
        break;
    }
        
    case strtype_Memory: {
        fprintf(fl, ", \"mem_buflen\":%ld", (long)str->buflen);

        long bufaddr;
        int elemsize;
        if (!str->unicode) {
            if (str->buf && str->buflen) {
                bufaddr = (*gli_dispatch_locate_arr)(str->buf, str->buflen, "&+#!Cn", str->arrayrock, &elemsize);
                fprintf(fl, ", \"mem_buf\":%ld", bufaddr);
                fprintf(fl, ", \"mem_bufptr\":%ld", str->bufptr - str->buf);
                fprintf(fl, ", \"mem_bufeof\":%ld", str->bufeof - str->buf);
                fprintf(fl, ", \"mem_bufend\":%ld", str->bufend - str->buf);
                if (elemsize) {
                    if (elemsize != 1)
                        gli_fatal_error("memstream encoding char array: wrong elemsize");
                    fprintf(fl, ",\n\"mem_bufdata\":");
                    print_string_len_json((char *)str->buf, str->buflen, fl);
                }
            }
        }
        else {
            if (str->ubuf && str->buflen) {
                bufaddr = (*gli_dispatch_locate_arr)(str->ubuf, str->buflen, "&+#!Iu", str->arrayrock, &elemsize);
                fprintf(fl, ", \"mem_buf\":%ld", bufaddr);
                fprintf(fl, ", \"mem_bufptr\":%ld", str->ubufptr - str->ubuf);
                fprintf(fl, ", \"mem_bufeof\":%ld", str->ubufeof - str->ubuf);
                fprintf(fl, ", \"mem_bufend\":%ld", str->ubufend - str->ubuf);
                if (elemsize) {
                    if (elemsize != 4)
                        gli_fatal_error("memstream encoding uni array: wrong elemsize");
                    fprintf(fl, ",\n\"mem_ubufdata\":");
                    print_ustring_len_json(str->ubuf, str->buflen, fl);
                }
            }
        }
        break;
    }
        
    case strtype_Resource: {
        if (str->isbinary)
            fprintf(fl, ", \"res_isbinary\":%d", str->isbinary);
        //### buf
        break;
    }
        
    }
    
    fprintf(fl, "}\n");
}

static void fileref_state_print(FILE *fl, frefid_t fref)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)fref->updatetag);
    fprintf(fl, ",\n\"rock\":%ld", (long)fref->rock);
    /* disprock is handled elsewhere */

    fprintf(fl, ",\n\"filename\":");
    print_string_json(fref->filename, fl);
    
    fprintf(fl, ",\n\"filetype\":%d", fref->filetype);
    fprintf(fl, ",\n\"textmode\":%d", fref->textmode);
    fprintf(fl, "}\n");
}


int glkunix_load_library_state(strid_t file, glkunix_unserialize_object_f extra_state_func, void *extra_state_rock)
{
    //###
    return FALSE;
}

