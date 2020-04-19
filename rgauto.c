/* rgauto.c: Support for autosave/autorestore
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>

#include "glk.h"
#include "remglk.h"

#define SERIAL_VERSION (1)

void library_state_print(FILE *fl)
{
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

    //### the extra stuff
    
    fprintf(fl, "}\n");
}
