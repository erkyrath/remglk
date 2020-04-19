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

void glkunix_save_library_state(strid_t file, glkunix_serialize_object_f extra_state_func, void *extra_state_rock)
{
    FILE *fl = file->file;
    
    fprintf(fl, "{\"type\":\"autosave\", \"version\":%d", SERIAL_VERSION);

    //### metrics

    //### windows, streams, filerefs

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
