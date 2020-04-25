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

#define UPDATETAG(obj) ((obj) ? (obj)->updatetag : 0)

static void window_state_print(FILE *fl, winid_t win)
{
    int first;
    int ix;
    
    fprintf(fl, "{\"tag\":\"%ld\"", (long)win->updatetag);
    fprintf(fl, ",\n\"type\":%ld, \"rock\":%ld", (long)win->type, (long)win->rock);
    /* disprock is handled elsewhere */

    fprintf(fl, ",\n\"bbox\":");
    data_grect_print(fl, &win->bbox);
    
    fprintf(fl, ",\n\"parenttag\":%ld", (long)UPDATETAG(win->parent));

    fprintf(fl, ",\n\"streamtag\":%ld", (long)UPDATETAG(win->str));
    fprintf(fl, ",\n\"echostreamtag\":%ld", (long)UPDATETAG(win->echostr));

    fprintf(fl, ",\n\"inputgen\":%ld", (long)win->inputgen);
    fprintf(fl, ",\n\"line_request\":%d", win->line_request);
    fprintf(fl, ",\n\"line_request_uni\":%d", win->line_request_uni);
    fprintf(fl, ",\n\"char_request\":%d", win->char_request);
    fprintf(fl, ",\n\"char_request_uni\":%d", win->char_request_uni);
    fprintf(fl, ",\n\"hyperlink_request\":%d", win->hyperlink_request);

    //### line buffer -- in subtypes really

    fprintf(fl, ",\n\"echo_line_input\":%d", win->echo_line_input);
    fprintf(fl, ",\n\"terminate_line_input\":%ld", (long)win->terminate_line_input);
    
    fprintf(fl, ",\n\"style\":%ld", (long)win->style);
    fprintf(fl, ",\n\"hyperlink\":%ld", (long)win->hyperlink);

    switch (win->type) {
        
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
        print_ustring_json(dwin->chars, dwin->numchars, fl);
        //### the text
        //### line buffer
        
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

static void stream_state_print(FILE *fl, strid_t str)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)str->updatetag);
    //###
    fprintf(fl, "}\n");
}

static void fileref_state_print(FILE *fl, frefid_t fref)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)fref->updatetag);
    //###
    fprintf(fl, "}\n");
}


int glkunix_load_library_state(strid_t file, glkunix_unserialize_object_f extra_state_func, void *extra_state_rock)
{
    //###
    return FALSE;
}

