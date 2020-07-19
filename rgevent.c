/* rgevent.c: Event handling, including glk_select() and timed input code
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "glk.h"
#include "remglk.h"
#include "rgdata.h"

/* A pointer to the place where the pending glk_select() will store its
    event. When not inside a glk_select() call, this will be NULL. */
static event_t *curevent = NULL;

/* The autosave code needs to peek at this. */
static glui32 last_event_type;

/* The current timed-event request, exactly as passed to
   glk_request_timer_events(). */
static glui32 timing_msec; 
/* The last timing value that was sent out. (0 means null was sent.) */
static glui32 last_timing_msec;
/* When the current timer started or last fired. */
static struct timeval timing_start; 

static glsi32 gli_timer_request_since_start(void);
static char *alloc_utf_buffer(glui32 *ustr, int ulen);

/* Set up the input system. This is called from main(). */
void gli_initialize_events()
{
    timing_msec = 0;
    last_timing_msec = 0;
    last_event_type = 0xFFFFFFFF;
}

void glk_select(event_t *event)
{
    curevent = event;
    gli_event_clearevent(curevent);
    
    if (gli_debugger)
        gidebug_announce_cycle(gidebug_cycle_InputWait);

    /* Send an update stanza to stdout. We do this before every glk_select,
       including at startup, but *not* if we just autorestored. */
    if (last_event_type != 0xFFFFFFFE) {
        gli_windows_update(NULL, TRUE);
        if (pref_singleturn) {
            /* Singleton mode mode means that we exit after every output. */
            gli_fast_exit();
        }
    }
    
    while (curevent->type == evtype_None) {
        data_event_t *data = data_event_read();
        
        window_t *win = NULL;
        glui32 val;

        if (data->gen != gli_window_current_generation() && data->dtag != dtag_Refresh)
            gli_fatal_error("Input generation number does not match.");

        switch (data->dtag) {
            case dtag_Refresh:
                /* Repeat the current display state and keep waiting for
                   a (real) event. */
                gli_windows_refresh(data->gen);
                gli_windows_update(NULL, FALSE);
                if (pref_singleturn) {
                    gli_fast_exit();
                }
                break;

            case dtag_Arrange:
                gli_windows_metrics_change(data->metrics);
                break;

            case dtag_Redraw:
                if (data->window)
                    win = glkunix_window_find_by_updatetag(data->window);
                else
                    win = NULL;
                gli_event_store(evtype_Redraw, win, 0, 0);
                break;

            case dtag_Line:
                win = glkunix_window_find_by_updatetag(data->window);
                if (!win)
                    break;
                if (!win->line_request)
                    break;
                gli_window_prepare_input(win, data->linevalue, data->linelen);
                gli_window_accept_line(win);
                win->inputgen = 0;
                break;

            case dtag_Char:
                win = glkunix_window_find_by_updatetag(data->window);
                if (!win)
                    break;
                if (!win->char_request)
                    break;
                val = data->charvalue;
                if (!win->char_request_uni) {
                    /* Filter out non-Latin-1 characters, except we also
                       accept special chars. */
                    if (val >= 256 && val < 0xffffffff-keycode_MAXVAL)
                        val = '?';
                }
                win->char_request = FALSE;
                win->char_request_uni = FALSE;
                win->inputgen = 0;
                gli_event_store(evtype_CharInput, win, val, 0);
                break;

            case dtag_Hyperlink:
                win = glkunix_window_find_by_updatetag(data->window);
                if (!win)
                    break;
                if (!win->hyperlink_request)
                    break;
                win->hyperlink_request = FALSE;
                gli_event_store(evtype_Hyperlink, win, data->linkvalue, 0);
                break;

            case dtag_Timer:
                gettimeofday(&timing_start, NULL);
                gli_event_store(evtype_Timer, NULL, 0, 0);
                break;

            case dtag_DebugInput:
                if (gli_debugger) {
                    /* If debug support is compiled in *and* turned on:
                       process the command, send an update, and
                       continue the glk_select. */
                    char *allocbuf = alloc_utf_buffer(data->linevalue, data->linelen);
                    gidebug_perform_command(allocbuf);
                    free(allocbuf);

                    gli_event_clearevent(curevent);
                    gli_windows_update(NULL, TRUE);
                    break;
                }
                /* ...else fall through to default behavior. */

            default:
                /* Ignore the event. (The constant is not defined by Glk;
                   we use it to represent any event whose textual name
                   is unrecognized.) */
                gli_event_store(0x7FFFFFFF, NULL, 0, 0);
                break;
        }

        data_event_free(data);
    }
    
    /* An event has occurred; glk_select() is over. */
    gli_windows_trim_buffers();
    last_event_type = curevent->type;
    curevent = NULL;

    if (gli_debugger)
        gidebug_announce_cycle(gidebug_cycle_InputAccept);
}

void glk_select_poll(event_t *event)
{
    curevent = event;
    gli_event_clearevent(curevent);

    /* We can only sensibly check for unfired timer events. */
    /* ### This is not consistent with the modern understanding that
       the display layer handles timer events. Might want to just rip
       all this timing code out entirely. */
    if (timing_msec) {
        glsi32 time = gli_timer_request_since_start();
        if (time >= 0 && time >= timing_msec) {
            gettimeofday(&timing_start, NULL);
            /* Resend timer request at next update. */
            last_timing_msec = 0;
            /* Call it a timer event. */
            curevent->type = evtype_Timer;
        }
    }

    curevent = NULL;
}

/* Wait for input, but it has to be a metrics object. Store the result. */
void gli_select_metrics(data_metrics_t *metrics, data_supportcaps_t *supportcaps)
{
    data_event_t *data = data_event_read();

    if (data->dtag != dtag_Init)
        gli_fatal_error("First input event must be 'init'");

    *metrics = *data->metrics;

    if (data->supportcaps) {
        *supportcaps = *data->supportcaps;
    }
    else {
        data_supportcaps_clear(supportcaps);
    }

    last_event_type = evtype_Arrange;

    data_event_free(data);
}

/* Wait for input, but it has to be a special-input response. (Currently
   this means a filename prompt.)
   Returns a malloced text string or NULL.
*/
char *gli_select_specialrequest(data_specialreq_t *special)
{
    char *buf = NULL;
    
    /* Send an update stanza to stdout. We do this before every
       get_by_prompt, but *not* if we just autorestored. */
    if (last_event_type != 0xFFFFFFFE) {
        gli_windows_update(special, TRUE);
        if (pref_singleturn) {
            /* Singleton mode mode means that we exit after every output. */
            gli_fast_exit();
        }
    }

    while (TRUE) {
        data_event_t *data = data_event_read();

        if (data->gen != gli_window_current_generation())
            gli_fatal_error("Input generation number does not match.");

        if (data->dtag != dtag_SpecialResponse) {
            data_event_free(data);
            continue;
        }
        
        if (data->linelen && data->linevalue) {
            int val;
            buf = malloc(data->linelen + 1);
            for (val=0; val<data->linelen; val++) {
                glui32 ch = data->linevalue[val];
                if (ch < 0x20 || ch > 0xFF)
                    ch = '-';
                buf[val] = ch;
            }
            buf[data->linelen] = '\0';
        }
        else {
            buf = NULL;
        };

        data_event_free(data);
        break;
    }

    /* This wasn't an event, but we want to nudge last_event_type. */
    last_event_type = evtype_None;
    
    return buf;
}

/* Increment the input counter. This is used with the fixedmetrics
   argument, which acts like the library got input.
*/
void gli_select_imaginary()
{
    last_event_type = evtype_Arrange;
}

/* Convert an array of Unicode chars to (null-terminated) UTF-8.
   The caller should free this after use.
*/
static char *alloc_utf_buffer(glui32 *ustr, int ulen)
{
    /* We do this in a lazy way; we alloc the largest possible buffer. */
    int len = 4*ulen+4;
    char *buf = malloc(len);
    if (!buf)
        return NULL;

    char *ptr = buf;
    int ix = 0;
    int cx = 0;
    while (cx < ulen) {
        ix += gli_encode_utf8(ustr[cx], ptr+ix, len-ix);
        cx++;
    }

    *(ptr+ix) = '\0';
    return buf;
}

#if GIDEBUG_LIBRARY_SUPPORT

/* Block and wait for debug commands. The library will accept debug commands
   until gidebug_perform_command() returns nonzero.

   This behaves a lot like glk_select(), except that it only handles debug
   input, not any of the standard event types.
*/
void gidebug_pause()
{
    if (!gli_debugger)
        return;

    gidebug_announce_cycle(gidebug_cycle_DebugPause);

    char *allocbuf;
    int unpause = FALSE;

    while (!unpause) {
        gli_windows_update(NULL, TRUE);

        data_event_t *data = data_event_read();

        if (data->gen != gli_window_current_generation() && data->dtag != dtag_Refresh)
            gli_fatal_error("Input generation number does not match.");

        switch (data->dtag) {
            case dtag_DebugInput:
                allocbuf = alloc_utf_buffer(data->linevalue, data->linelen);
                unpause = gidebug_perform_command(allocbuf);
                free(allocbuf);
                break;

            default:
                /* Ignore all non-debug events. */
                break;
        }
        
    }    
    
    gidebug_announce_cycle(gidebug_cycle_DebugUnpause);
}

#endif /* GIDEBUG_LIBRARY_SUPPORT */

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

/* Peek at the last Glk event to come in. Returns 0xFFFFFFFF if we just
   started up; 0xFFFFFFFE if we just autorestored. */
glui32 glkunix_get_last_event_type()
{
    return last_event_type;
}

void gli_set_last_event_type(glui32 type)
{
    last_event_type = type;
}

void glk_request_timer_events(glui32 millisecs)
{
    if (!gli_supportcaps.timer)
        return;
    timing_msec = millisecs;
    gettimeofday(&timing_start, NULL);
}

/* Return whether the timer request has changed since the last call.
   If so, also return the request value as *msec. */
int gli_timer_need_update(glui32 *msec)
{
    if (last_timing_msec != timing_msec) {
        *msec = timing_msec;
        last_timing_msec = timing_msec;
        return TRUE;
    }
    else {
        *msec = 0;
        return FALSE;
    }
}

glui32 gli_timer_get_timing_msec()
{
    return timing_msec;
}

/* Work out how many milliseconds it has been since timing_start.
   If there is no timer, returns -1. */
static glsi32 gli_timer_request_since_start()
{
    struct timeval tv;

    if (!gli_supportcaps.timer)
        return -1;
    if (!timing_msec)
        return -1;

    gettimeofday(&tv, NULL);

    if (tv.tv_sec < timing_start.tv_sec) {
        return 0;
    }
    else if (tv.tv_sec == timing_start.tv_sec) {
        if (tv.tv_usec < timing_start.tv_usec)
            return 0;
        return (tv.tv_usec - timing_start.tv_usec) / 1000;
    }
    else {
        glsi32 res = (tv.tv_sec - timing_start.tv_sec) * 1000;
        res += ((tv.tv_usec - timing_start.tv_usec) / 1000);
        return res;
    }
}
