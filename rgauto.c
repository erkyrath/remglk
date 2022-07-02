/* rgauto.c: Support for autosave/autorestore
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
#include "rgdata_int.h"
#include "rgwin_pair.h"
#include "rgwin_buf.h"
#include "rgwin_grid.h"
#include "rgwin_graph.h"

#define SERIAL_VERSION (1)

static void window_state_print(FILE *fl, winid_t win);
static void stream_state_print(FILE *fl, strid_t str);
static void fileref_state_print(FILE *fl, frefid_t fref);
static int window_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, winid_t win);
static int stream_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, strid_t str);
static int fileref_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, frefid_t fref);
static void tbrun_print(FILE *fl, tbrun_t *run);
static void tgline_print(FILE *fl, tgline_t *line, int width);

static window_t *libstate_window_find_by_updatetag(glkunix_library_state_t state, glui32 tag);
static stream_t *libstate_stream_find_by_updatetag(glkunix_library_state_t state, glui32 tag);

/* Import a new library_state into the current Glk library globals. Existing data objects are closed and new ones are opened.

   The passed-in state object cleared out in the process and cannot be reused. (You should still call glkunix_library_state_free on it afterwards.)
 */
glui32 glkunix_update_from_library_state(glkunix_library_state_t state)
{
    /* First close all the windows and streams and filerefs. (It only really matters for streams, which need to be flushed, but it's cleaner to close everything.) */
    if (gli_rootwin) {
        /* This takes care of all the windows. */
        glk_window_close(gli_rootwin, NULL);
    }
    while (TRUE) {
        strid_t str = glk_stream_iterate(NULL, NULL);
        if (!str) break;
        glk_stream_close(str, NULL); 
    }
    while (TRUE) {
        frefid_t fref = glk_fileref_iterate(NULL, NULL);
        if (!fref) break;
        glk_fileref_destroy(fref); 
    }
    glk_request_timer_events(0);

    if (glk_window_iterate(NULL, NULL) || glk_stream_iterate(NULL, NULL) || glk_fileref_iterate(NULL, NULL)) {
        gli_fatal_error("unclosed objects remain!");
        return FALSE;
    }
    if (gli_rootwin || gli_currentstr) {
        gli_fatal_error("root references remain!");
        return FALSE;
    }

    /* Begin updating the library. */
    
    gli_windows_update_metrics(state->metrics);
    gli_supportcaps = *state->supportcaps;
    
    /* Transfer in all the data objects. They are already correctly linked to each other (e.g., each win->str refers to a stream object in the state) so we just have to shove the chains into place. */

    gli_windows_update_from_state(state->windowlist, state->windowcount, state->rootwin, state->generation);
    gli_streams_update_from_state(state->streamlist, state->streamcount, state->currentstr);
    gli_filerefs_update_from_state(state->filereflist, state->filerefcount);
    
    glk_request_timer_events(state->timerinterval);
    gli_set_last_event_type(0xFFFFFFFE);

    /* At this point, state no longer owns its object references. Clean them out to avoid problems later. */
    if (state->windowlist) {
        free(state->windowlist);
        state->windowlist = NULL;
    }
    if (state->streamlist) {
        free(state->streamlist);
        state->streamlist = NULL;
    }
    if (state->filereflist) {
        free(state->filereflist);
        state->filereflist = NULL;
    }
    state->rootwin = NULL;
    state->currentstr = NULL;
    
    return TRUE;
}

void glkunix_save_library_state(strid_t file, strid_t omitstream, glkunix_serialize_object_f extra_state_func, void *extra_state_rock)
{
    FILE *fl = file->file;
    winid_t tmpwin;
    strid_t tmpstr;
    frefid_t tmpfref;
    int first;
    
    fprintf(fl, "{\"type\":\"autosave\", \"version\":%d", SERIAL_VERSION);

    /* We store generation+1, because the upcoming gli_windows_update is going to increment the generation. We want to match that. */
    glui32 newgen = gli_window_current_generation() + 1;
    fprintf(fl, ",\n\"generation\":%ld", (long)newgen);

    fprintf(fl, ",\n\"metrics\":");
    data_metrics_print(fl, gli_windows_get_metrics());

    fprintf(fl, ",\n\"supportcaps\":");
    data_supportcaps_print(fl, &gli_supportcaps);

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
        if (tmpstr == omitstream) continue;
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
    
    fprintf(fl, "{\"tag\":%ld", (long)win->updatetag);
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

    /* Dirty flags will not be saved here. Autosave occurs just before
       the glk_select call. So even though dirty flags exist at this point,
       we're about to generate a window update and clear them all. At
       autorestore time, a continuously-connected client will be up to date
       and should not see any dirtiness. */
    
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
        
        /* We don't save the updatemark/startclear. */
        
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
            fprintf(fl, ",\n\"buf_ininput\":%d", 1);
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

        /* We don't save the dirty flags. */

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
            fprintf(fl, ",\n\"grid_ininput\":%d", 1);
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

        /* We don't save the updatemark. */

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
    fprintf(fl, "{\"tag\":%ld", (long)str->updatetag);
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
        /* lastop will be reset when the file is reopened */
        if (str->filename) {
            fprintf(fl, ", \"file_filename\":");
            print_string_json(str->filename, fl);
        }
        if (str->modestr) {
            fprintf(fl, ", \"file_modestr\":");
            print_string_json(str->modestr, fl);
        }
        long pos = glk_stream_get_position(str);
        fprintf(fl, ", \"file_filepos\":%ld", pos);
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
                fprintf(fl, ", \"mem_ubuf\":%ld", bufaddr);
                fprintf(fl, ", \"mem_ubufptr\":%ld", str->ubufptr - str->ubuf);
                fprintf(fl, ", \"mem_ubufeof\":%ld", str->ubufeof - str->ubuf);
                fprintf(fl, ", \"mem_ubufend\":%ld", str->ubufend - str->ubuf);
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
        fprintf(fl, ", \"res_fileresnum\":%ld", (long)str->fileresnum);

        fprintf(fl, ", \"res_buflen\":%ld", (long)str->buflen);

        if (!str->unicode) {
            if (str->buf && str->buflen) {
                fprintf(fl, ", \"res_bufptr\":%ld", str->bufptr - str->buf);
                fprintf(fl, ", \"res_bufeof\":%ld", str->bufeof - str->buf);
                fprintf(fl, ", \"res_bufend\":%ld", str->bufend - str->buf);
                fprintf(fl, ",\n\"res_bufdata\":");
                print_string_len_json((char *)str->buf, str->buflen, fl);
            }
        }
        else {
            if (str->ubuf && str->buflen) {
                fprintf(fl, ", \"res_ubufptr\":%ld", str->ubufptr - str->ubuf);
                fprintf(fl, ", \"res_ubufeof\":%ld", str->ubufeof - str->ubuf);
                fprintf(fl, ", \"res_ubufend\":%ld", str->ubufend - str->ubuf);
                fprintf(fl, ",\n\"res_ubufdata\":");
                print_ustring_len_json(str->ubuf, str->buflen, fl);
            }
        }
        
        break;
    }
        
    }
    
    fprintf(fl, "}\n");
}

static void fileref_state_print(FILE *fl, frefid_t fref)
{
    fprintf(fl, "{\"tag\":%ld", (long)fref->updatetag);
    fprintf(fl, ",\n\"rock\":%ld", (long)fref->rock);
    /* disprock is handled elsewhere */

    fprintf(fl, ",\n\"filename\":");
    print_string_json(fref->filename, fl);
    
    fprintf(fl, ",\n\"filetype\":%d", fref->filetype);
    fprintf(fl, ",\n\"textmode\":%d", fref->textmode);
    fprintf(fl, "}\n");
}

/* We don't load the library state into our live library. Rather, it goes into a glkunix_library_state_t object, which can be brought live later. 
*/
glkunix_library_state_t glkunix_load_library_state(strid_t file, glkunix_unserialize_object_f extra_state_func, void *extra_state_rock)
{
    FILE *fl = file->file;
    int ix;
    
    struct glkunix_unserialize_context_struct ctx;
    if (!glkunix_unserialize_object_root(fl, &ctx))
        return NULL;

    glui32 version;
    if (!glkunix_unserialize_uint32(&ctx, "version", &version)) {
        gli_fatal_error("Autorestore serial version not found");
        return NULL;
    }
    if (version <= 0 || version > SERIAL_VERSION) {
        gli_fatal_error("Autorestore serial version not supported");
        return NULL;
    }

    glkunix_library_state_t state = glkunix_library_state_alloc();

    glkunix_unserialize_context_t dat;
    glkunix_unserialize_context_t array;
    glkunix_unserialize_context_t entry;
    int count;
    glui32 tag;

    glkunix_unserialize_uint32(&ctx, "generation", &state->generation);

    if (glkunix_unserialize_struct(&ctx, "metrics", &dat)) {
        state->metrics = data_metrics_parse(dat->dat);
    }
    if (glkunix_unserialize_list(&ctx, "supportcaps", &dat, &count)) {
        state->supportcaps = data_supportcaps_parse(dat->dat);
    }

    /* First we create blank Glk object structures, filling in only the updatetags. (We have to do this before dealing with the object data, because objects can refer to each other.) */
    
    if (glkunix_unserialize_list(&ctx, "windows", &array, &count)) {
        state->windowcount = count;
        state->windowlist = (window_t **)malloc(state->windowcount * sizeof(window_t *));
        for (ix=0; ix<state->windowcount; ix++) {
            state->windowlist[ix] = gli_window_alloc_inactive();
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            if (!glkunix_unserialize_uint32(entry, "tag", &state->windowlist[ix]->updatetag))
                return NULL;
        }
    }

    if (glkunix_unserialize_list(&ctx, "streams", &array, &count)) {
        state->streamcount = count;
        state->streamlist = (stream_t **)malloc(state->streamcount * sizeof(stream_t *));
        for (ix=0; ix<state->streamcount; ix++) {
            state->streamlist[ix] = gli_stream_alloc_inactive();
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            if (!glkunix_unserialize_uint32(entry, "tag", &state->streamlist[ix]->updatetag))
                return NULL;
        }
    }

    if (glkunix_unserialize_list(&ctx, "filerefs", &array, &count)) {
        state->filerefcount = count;
        state->filereflist = (fileref_t **)malloc(state->filerefcount * sizeof(fileref_t *));
        for (ix=0; ix<state->filerefcount; ix++) {
            state->filereflist[ix] = gli_fileref_alloc_inactive();
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            if (!glkunix_unserialize_uint32(entry, "tag", &state->filereflist[ix]->updatetag))
                return NULL;
        }
    }

    /* Now we unserialize all the object data. */

    if (glkunix_unserialize_list(&ctx, "windows", &array, &count)) {
        for (ix=0; ix<state->windowcount; ix++) {
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            window_t *win = state->windowlist[ix];
            if (!window_state_parse(state, entry, win))
                return NULL;
        }
    }

    if (glkunix_unserialize_list(&ctx, "streams", &array, &count)) {
        for (ix=0; ix<state->streamcount; ix++) {
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            stream_t *str = state->streamlist[ix];
            if (!stream_state_parse(state, entry, str))
                return NULL;
        }
    }
    
    if (glkunix_unserialize_list(&ctx, "filerefs", &array, &count)) {
        for (ix=0; ix<state->filerefcount; ix++) {
            if (!glkunix_unserialize_list_entry(array, ix, &entry))
                return NULL;
            fileref_t *fref = state->filereflist[ix];
            if (!fileref_state_parse(state, entry, fref))
                return NULL;
        }
    }

    entry = NULL;

    glkunix_unserialize_uint32(&ctx, "timerinterval", &state->timerinterval);

    if (glkunix_unserialize_uint32(&ctx, "rootwintag", &tag)) {
        state->rootwin = libstate_window_find_by_updatetag(state, tag);
        if (!state->rootwin)
            gli_fatal_error("Could not locate rootwin");
    }

    if (glkunix_unserialize_uint32(&ctx, "currentstrtag", &tag)) {
        state->currentstr = libstate_stream_find_by_updatetag(state, tag);
        if (!state->currentstr)
            gli_fatal_error("Could not locate currentstr");
    }

    if (extra_state_func) {
        if (!glkunix_unserialize_struct(&ctx, "extra_state", &dat)) {
            gli_fatal_error("Autorestore extra state not found");
            return NULL;
        }

        if (!extra_state_func(dat, extra_state_rock)) {
            gli_fatal_error("Autorestore extra state failed");
            return NULL;
        }
    }

    entry = NULL;
    array = NULL;
    dat = NULL;
    glkunix_unserialize_object_root_finalize(&ctx);
    
    return state;
}

static int window_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, winid_t win)
{
    int ix;
    int intval;
    glkunix_unserialize_context_t array;
    glkunix_unserialize_context_t el;
    int count;
    glui32 tag;
    
    glkunix_unserialize_uint32(entry, "rock", &win->rock);
    glkunix_unserialize_uint32(entry, "type", &win->type);
    /* disprock is handled elsewhere */
    
    glkunix_unserialize_context_t boxentry;
    if (glkunix_unserialize_struct(entry, "bbox", &boxentry)) {
        data_grect_parse(boxentry->dat, &win->bbox);
    }
    
    if (glkunix_unserialize_uint32(entry, "parenttag", &tag)) {
        win->parent = libstate_window_find_by_updatetag(state, tag);
    }
    if (glkunix_unserialize_uint32(entry, "streamtag", &tag)) {
        win->str = libstate_stream_find_by_updatetag(state, tag);
    }
    if (glkunix_unserialize_uint32(entry, "echostreamtag", &tag)) {
        win->echostr = libstate_stream_find_by_updatetag(state, tag);
    }
    
    glkunix_unserialize_uint32(entry, "inputgen", &win->inputgen);
    glkunix_unserialize_int(entry, "line_request", &win->line_request);
    glkunix_unserialize_int(entry, "line_request_uni", &win->line_request_uni);
    glkunix_unserialize_int(entry, "char_request", &win->char_request);
    glkunix_unserialize_int(entry, "char_request_uni", &win->char_request_uni);
    glkunix_unserialize_int(entry, "hyperlink_request", &win->hyperlink_request);
    glkunix_unserialize_int(entry, "echo_line_input", &win->echo_line_input);
    glkunix_unserialize_uint32(entry, "terminate_line_input", &win->terminate_line_input);
    glkunix_unserialize_uint32(entry, "style", &win->style);
    glkunix_unserialize_uint32(entry, "hyperlink", &win->hyperlink);
    
    switch (win->type) {

    case wintype_Pair: {
        window_pair_t *dwin = win_pair_create(win, 0, NULL, 0);
        win->data = dwin;
        
        if (glkunix_unserialize_uint32(entry, "pair_child1tag", &tag)) {
            dwin->child1 = libstate_window_find_by_updatetag(state, tag);
        }
        if (glkunix_unserialize_uint32(entry, "pair_child2tag", &tag)) {
            dwin->child2 = libstate_window_find_by_updatetag(state, tag);
        }
        if (glkunix_unserialize_uint32(entry, "pair_keytag", &tag)) {
            dwin->key = libstate_window_find_by_updatetag(state, tag);
        }
        
        glkunix_unserialize_int(entry, "pair_splitpos", &dwin->splitpos);
        glkunix_unserialize_int(entry, "pair_splitwidth", &dwin->splitwidth);
        glkunix_unserialize_uint32(entry, "pair_dir", &dwin->dir);
        glkunix_unserialize_int(entry, "pair_vertical", &dwin->vertical);
        glkunix_unserialize_int(entry, "pair_backward", &dwin->backward);
        glkunix_unserialize_int(entry, "pair_hasborder", &dwin->hasborder);
        glkunix_unserialize_uint32(entry, "pair_division", &dwin->division);
        glkunix_unserialize_uint32(entry, "pair_size", &dwin->size);
        break;
    }

    case wintype_TextBuffer: {
        window_textbuffer_t *dwin = win_textbuffer_create(win);
        win->data = dwin;

        glkunix_unserialize_int(entry, "buf_width", &dwin->width);
        glkunix_unserialize_int(entry, "buf_height", &dwin->height);

        glui32 *buf;
        long bufcount;
        if (glkunix_unserialize_len_unicode(entry, "buf_chars", &buf, &bufcount)) {
            if (bufcount > dwin->charssize) {
                dwin->charssize = bufcount;
                dwin->chars = (glui32 *)realloc(dwin->chars, dwin->charssize * sizeof(glui32));
            }
            for (ix=0; ix<bufcount; ix++)
                dwin->chars[ix] = buf[ix];
            dwin->numchars = bufcount;
            free(buf);
        }
        
        if (glkunix_unserialize_list(entry, "buf_runs", &array, &count)) {
            if (count > dwin->runssize) {
                dwin->runssize = (count | 7) + 1 + 8;
                dwin->runs = (tbrun_t *)realloc(dwin->runs, dwin->runssize * sizeof(tbrun_t));
            }
            if (!dwin->runs)
                return FALSE;
            memset(dwin->runs, 0, dwin->runssize * sizeof(tbrun_t));
            dwin->numruns = count;
            for (ix=0; ix<count; ix++) {
                tbrun_t *run = &dwin->runs[ix];
                if (!glkunix_unserialize_list_entry(array, ix, &el))
                    return FALSE;
                glkunix_unserialize_int(el, "style", &intval);
                run->style = intval;
                glkunix_unserialize_uint32(el, "hyperlink", &run->hyperlink);
                glkunix_unserialize_long(el, "pos", &run->pos);
                if (!glkunix_unserialize_long(el, "specialnum", &run->specialnum)) {
                    run->specialnum = -1; /* default value */
                }
            }
        }

        if (glkunix_unserialize_list(entry, "buf_specials", &array, &count)) {
            if (count > dwin->specialssize) {
                dwin->specialssize = (count | 7) + 1 + 8;
                dwin->specials = (data_specialspan_t **)realloc(dwin->specials, dwin->specialssize * sizeof(data_specialspan_t *));
            }
            if (!dwin->specials)
                return FALSE;
            dwin->numspecials = count;
            for (ix=0; ix<count; ix++) {
                if (!glkunix_unserialize_list_entry(array, ix, &el))
                    return FALSE;
                dwin->specials[ix] = data_specialspan_auto_parse(el->dat);
                if (!dwin->specials[ix])
                    return FALSE;
            }
        }

        intval = FALSE;
        if (glkunix_unserialize_int(entry, "buf_ininput", &intval) && intval) {
            glkunix_unserialize_uint32(entry, "buf_incurpos", &dwin->incurpos);
            glkunix_unserialize_int(entry, "buf_inunicode", &dwin->inunicode);
            glkunix_unserialize_int(entry, "buf_inecho", &dwin->inecho);
            glkunix_unserialize_uint32(entry, "buf_intermkeys", &dwin->intermkeys);
            glkunix_unserialize_int(entry, "buf_inmax", &dwin->inmax);
            glkunix_unserialize_uint32(entry, "buf_origstyle", &dwin->origstyle);
            glkunix_unserialize_uint32(entry, "buf_orighyperlink", &dwin->orighyperlink);

            win->tempbufinfo = data_tempbufinfo_alloc();
            if (!dwin->inunicode) {
                glkunix_unserialize_long(entry, "buf_line_buffer", &win->tempbufinfo->bufkey);
                glkunix_unserialize_len_bytes(entry, "buf_line_buffer_data", &win->tempbufinfo->bufdata, &win->tempbufinfo->bufdatalen);
            }
            else {
                glkunix_unserialize_long(entry, "buf_line_buffer", &win->tempbufinfo->bufkey);
                glkunix_unserialize_len_unicode(entry, "buf_line_buffer_data", &win->tempbufinfo->ubufdata, &win->tempbufinfo->bufdatalen);
            }
        }

        /* Clear dirty flags. */
        dwin->updatemark = dwin->numchars;
        dwin->startclear = FALSE;

        break;
    }

    case wintype_TextGrid: {
        window_textgrid_t *dwin = win_textgrid_create(win);
        win->data = dwin;

        glkunix_unserialize_int(entry, "grid_width", &dwin->width);
        glkunix_unserialize_int(entry, "grid_height", &dwin->height);
        glkunix_unserialize_int(entry, "grid_curx", &dwin->curx);
        glkunix_unserialize_int(entry, "grid_cury", &dwin->cury);

        dwin->linessize = dwin->height+1;
        dwin->lines = (tgline_t *)malloc(dwin->linessize * sizeof(tgline_t));
        if (!dwin->lines)
            return FALSE;
        win_textgrid_alloc_lines(dwin, 0, dwin->linessize, dwin->width);
        
        if (glkunix_unserialize_list(entry, "grid_lines", &array, &count)) {
            for (ix=0; ix<count && ix<dwin->height; ix++) {
                tgline_t *line = &dwin->lines[ix];
                if (!glkunix_unserialize_list_entry(array, ix, &el))
                    return FALSE;
                
                glkunix_unserialize_context_t subarray;
                int jx;
                glui32 *ubuf;
                long buflen;
                int len;
                if (glkunix_unserialize_len_unicode(el, "chars", &ubuf, &buflen)) {
                    for (jx=0; jx<buflen && jx<dwin->width; jx++) {
                        line->chars[jx] = ubuf[jx];
                    }
                    free(ubuf);
                }
                if (glkunix_unserialize_list(el, "styles", &subarray, &len)) {
                    for (jx=0; jx<len && jx<dwin->width; jx++) {
                        if (!glkunix_unserialize_uint32_list_entry(subarray, jx, &tag))
                            return FALSE;
                        line->styles[jx] = tag;
                    }
                }
                if (glkunix_unserialize_list(el, "links", &subarray, &len)) {
                    for (jx=0; jx<len && jx<dwin->width; jx++) {
                        if (!glkunix_unserialize_uint32_list_entry(subarray, jx, &tag))
                            return FALSE;
                        line->links[jx] = tag;
                    }
                }

            }
        }
        
        intval = FALSE;
        if (glkunix_unserialize_int(entry, "grid_ininput", &intval) && intval) {
            glkunix_unserialize_uint32(entry, "grid_incurpos", &dwin->incurpos);
            glkunix_unserialize_int(entry, "grid_inunicode", &dwin->inunicode);
            glkunix_unserialize_int(entry, "grid_inecho", &dwin->inecho);
            glkunix_unserialize_uint32(entry, "grid_intermkeys", &dwin->intermkeys);
            glkunix_unserialize_int(entry, "grid_inmax", &dwin->inmax);
            glkunix_unserialize_int(entry, "grid_inoriglen", &dwin->inoriglen);
            glkunix_unserialize_uint32(entry, "grid_origstyle", &dwin->origstyle);
            /*
            glkunix_unserialize_uint32(entry, "grid_orighyperlink", &dwin->orighyperlink);
            */

            win->tempbufinfo = data_tempbufinfo_alloc();
            if (!dwin->inunicode) {
                glkunix_unserialize_long(entry, "grid_line_buffer", &win->tempbufinfo->bufkey);
                glkunix_unserialize_len_bytes(entry, "grid_line_buffer_data", &win->tempbufinfo->bufdata, &win->tempbufinfo->bufdatalen);
            }
            else {
                glkunix_unserialize_long(entry, "grid_line_buffer", &win->tempbufinfo->bufkey);
                glkunix_unserialize_len_unicode(entry, "grid_line_buffer_data", &win->tempbufinfo->ubufdata, &win->tempbufinfo->bufdatalen);
            }
        }

        /* Clear dirty flags. */
        for (ix=0; ix<dwin->linessize; ix++) {
            dwin->lines[ix].dirty = FALSE;
        }
        dwin->alldirty = FALSE;
        
        break;
    }

    case wintype_Graphics: {
        window_graphics_t *dwin = win_graphics_create(win);
        win->data = dwin;

        glkunix_unserialize_int(entry, "graph_width", &dwin->graphwidth);
        glkunix_unserialize_int(entry, "graph_height", &dwin->graphheight);

        if (glkunix_unserialize_list(entry, "graph_content", &array, &count)) {
            if (count > dwin->contentsize) {
                dwin->contentsize = (count | 7) + 1 + 8;
                dwin->content = (data_specialspan_t **)realloc(dwin->content, dwin->contentsize * sizeof(data_specialspan_t *));
            }
            if (!dwin->content)
                return FALSE;
            dwin->numcontent = count;
            for (ix=0; ix<count; ix++) {
                if (!glkunix_unserialize_list_entry(array, ix, &el))
                    return FALSE;
                dwin->content[ix] = data_specialspan_auto_parse(el->dat);
                if (!dwin->content[ix])
                    return FALSE;
            }
        }

        /* Clear dirty flags. */
        dwin->updatemark = dwin->numcontent;
        
        break;
    }
        
    }

    return TRUE;
}

static int stream_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, strid_t str)
{
    glui32 tag;
    
    glkunix_unserialize_uint32(entry, "rock", &str->rock);
    glkunix_unserialize_int(entry, "type", &str->type);
    /* disprock is handled elsewhere */
    glkunix_unserialize_int(entry, "unicode", &str->unicode);
    glkunix_unserialize_int(entry, "readable", &str->readable);
    glkunix_unserialize_int(entry, "writable", &str->writable);
    glkunix_unserialize_uint32(entry, "readcount", &str->readcount);
    glkunix_unserialize_uint32(entry, "writecount", &str->writecount);

    switch (str->type) {
        
    case strtype_Window:
        if (glkunix_unserialize_uint32(entry, "win_tag", &tag)) {
            str->win = libstate_window_find_by_updatetag(state, tag);
        }
        break;
        
    case strtype_File:
        glkunix_unserialize_int(entry, "file_isbinary", &str->isbinary);
        glkunix_unserialize_latin1_string(entry, "file_filename", &str->filename);
        glkunix_unserialize_latin1_string(entry, "file_modestr", &str->modestr);
        str->tempbufinfo = data_tempbufinfo_alloc();
        glkunix_unserialize_uint32(entry, "file_filepos", &str->tempbufinfo->bufptr);
        /* we'll open the file itself later */
        break;
        
    case strtype_Memory:
        glkunix_unserialize_uint32(entry, "mem_buflen", &str->buflen);
        str->tempbufinfo = data_tempbufinfo_alloc();
        if (!str->unicode) {
            glkunix_unserialize_long(entry, "mem_buf", &str->tempbufinfo->bufkey);
            glkunix_unserialize_uint32(entry, "mem_bufptr", &str->tempbufinfo->bufptr);
            glkunix_unserialize_uint32(entry, "mem_bufeof", &str->tempbufinfo->bufeof);
            glkunix_unserialize_uint32(entry, "mem_bufend", &str->tempbufinfo->bufend);
            glkunix_unserialize_len_bytes(entry, "mem_bufdata", &str->tempbufinfo->bufdata, &str->tempbufinfo->bufdatalen);
        }
        else {
            glkunix_unserialize_long(entry, "mem_ubuf", &str->tempbufinfo->bufkey);
            glkunix_unserialize_uint32(entry, "mem_ubufptr", &str->tempbufinfo->bufptr);
            glkunix_unserialize_uint32(entry, "mem_ubufeof", &str->tempbufinfo->bufeof);
            glkunix_unserialize_uint32(entry, "mem_ubufend", &str->tempbufinfo->bufend);
            glkunix_unserialize_len_unicode(entry, "mem_ubufdata", &str->tempbufinfo->ubufdata, &str->tempbufinfo->bufdatalen);
        }
        break;
        
    case strtype_Resource:
        glkunix_unserialize_int(entry, "res_isbinary", &str->isbinary);
        glkunix_unserialize_uint32(entry, "res_fileresnum", &str->fileresnum);
        glkunix_unserialize_uint32(entry, "res_buflen", &str->buflen);
        str->tempbufinfo = data_tempbufinfo_alloc();
        if (!str->unicode) {
            glkunix_unserialize_uint32(entry, "res_bufptr", &str->tempbufinfo->bufptr);
            glkunix_unserialize_uint32(entry, "res_bufeof", &str->tempbufinfo->bufeof);
            glkunix_unserialize_uint32(entry, "res_bufend", &str->tempbufinfo->bufend);
            glkunix_unserialize_len_bytes(entry, "res_bufdata", &str->tempbufinfo->bufdata, &str->tempbufinfo->bufdatalen);
        }
        else {
            glkunix_unserialize_uint32(entry, "res_ubufptr", &str->tempbufinfo->bufptr);
            glkunix_unserialize_uint32(entry, "res_ubufeof", &str->tempbufinfo->bufeof);
            glkunix_unserialize_uint32(entry, "res_ubufend", &str->tempbufinfo->bufend);
            glkunix_unserialize_len_unicode(entry, "res_ubufdata", &str->tempbufinfo->ubufdata, &str->tempbufinfo->bufdatalen);
        }
        break;
        
    }

    return TRUE;
}

static int fileref_state_parse(glkunix_library_state_t state, glkunix_unserialize_context_t entry, frefid_t fref)
{
    glkunix_unserialize_uint32(entry, "rock", &fref->rock);
    /* disprock is handled elsewhere */
    glkunix_unserialize_latin1_string(entry, "filename", &fref->filename);
    glkunix_unserialize_int(entry, "filetype", &fref->filetype);
    glkunix_unserialize_int(entry, "textmode", &fref->textmode);

    return TRUE;
}

static window_t *libstate_window_find_by_updatetag(glkunix_library_state_t state, glui32 tag)
{
    int ix;
    for (ix=0; ix<state->windowcount; ix++) {
        if (state->windowlist[ix]->updatetag == tag)
            return state->windowlist[ix];
    }
    return NULL;
}

static stream_t *libstate_stream_find_by_updatetag(glkunix_library_state_t state, glui32 tag)
{
    int ix;
    for (ix=0; ix<state->streamcount; ix++) {
        if (state->streamlist[ix]->updatetag == tag)
            return state->streamlist[ix];
    }
    return NULL;
}

