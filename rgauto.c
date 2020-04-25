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

#define SERIAL_VERSION (1)

static void window_state_print(FILE *fl, winid_t win);
static void stream_state_print(FILE *fl, strid_t str);
static void fileref_state_print(FILE *fl, frefid_t fref);

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

/* We don't use data_window_print (etc) here because we need a complete state dump for the autosave. */

#define UPDATETAG(obj) ((obj) ? (obj)->updatetag : 0)

static void window_state_print(FILE *fl, winid_t win)
{
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

    //### line buffer

    fprintf(fl, ",\n\"echo_line_input\":%d", win->echo_line_input);
    fprintf(fl, ",\n\"terminate_line_input\":%ld", (long)win->terminate_line_input);
    
    fprintf(fl, ",\n\"style\":%ld", (long)win->style);
    fprintf(fl, ",\n\"hyperlink\":%ld", (long)win->hyperlink);

    //### subtype data

    fprintf(fl, "}\n");
}

static void stream_state_print(FILE *fl, strid_t str)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)str->updatetag);
    fprintf(fl, "}\n");
}

static void fileref_state_print(FILE *fl, frefid_t fref)
{
    fprintf(fl, "{\"tag\":\"%ld\"", (long)fref->updatetag);
    fprintf(fl, "}\n");
}


int glkunix_load_library_state(strid_t file, glkunix_unserialize_object_f extra_state_func, void *extra_state_rock)
{
    //###
    return FALSE;
}

