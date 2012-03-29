/* gtevent.c: Event handling, including glk_select() and timed input code
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

/* A pointer to the place where the pending glk_select() will store its
    event. When not inside a glk_select() call, this will be NULL. */
static event_t *curevent = NULL; 

static glui32 timing_msec; /* The current timed-event request, exactly as
    passed to glk_request_timer_events(). */

/* Set up the input system. This is called from main(). */
void gli_initialize_events()
{
    timing_msec = 0;
}

void glk_select(event_t *event)
{
    curevent = event;
    gli_event_clearevent(curevent);
    
    gli_windows_update();
    
    while (curevent->type == evtype_None) {
        data_input_t *data = data_input_read();

        switch (data->dtag) {
            case dtag_Arrange:
                gli_windows_metrics_change(data->metrics);
                break;

            /* ### */

            default:
                /* Ignore the event. */
                break;
        }

        data_input_free(data);
    }
    
    /* An event has occurred; glk_select() is over. */
    gli_windows_trim_buffers();
    curevent = NULL;
}

void glk_select_poll(event_t *event)
{
    int firsttime = TRUE;
    
    curevent = event;
    gli_event_clearevent(curevent);
    
    gli_windows_update();
    
    /* Now we check, once, all the stuff that glk_select() checks
        periodically. This includes rearrange events and timer events. 
       Yes, this looks like a loop, but that's just so we can use
        continue; it executes exactly once. */
        
    while (firsttime) {
        firsttime = FALSE;

    }

    curevent = NULL;
}

/* Various modules can call this to indicate that an event has occurred.
    This doesn't try to queue events, but since a single keystroke or
    idle event can only cause one event at most, this is fine. */
void gli_event_store(glui32 type, window_t *win, glui32 val1, glui32 val2)
{
    if (curevent) {
        curevent->type = type;
        curevent->win = win;
        curevent->val1 = val1;
        curevent->val2 = val2;
    }
}

void glk_request_timer_events(glui32 millisecs)
{
    timing_msec = millisecs;
    /* ### */
}
