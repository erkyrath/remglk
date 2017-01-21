/* gtevent.c: Event handling, including glk_select() and timed input code
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
}

void glk_select(event_t *event)
{
    curevent = event;
    gli_event_clearevent(curevent);
    
    if (gli_debugger)
        gidebug_announce_cycle(gidebug_cycle_InputWait);

    gli_windows_update(NULL, TRUE);
    
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
                break;

            case dtag_Arrange:
                gli_windows_metrics_change(data->metrics);
                break;

            case dtag_Redraw:
                if (data->window)
                    win = gli_window_find_by_tag(data->window);
                else
                    win = NULL;
                gli_event_store(evtype_Redraw, win, 0, 0);
                break;

            case dtag_Line:
                win = gli_window_find_by_tag(data->window);
                if (!win)
                    break;
                if (!win->line_request)
                    break;
                gli_window_prepare_input(win, data->linevalue, data->linelen);
                gli_window_accept_line(win);
                win->inputgen = 0;
                break;

            case dtag_Char:
                win = gli_window_find_by_tag(data->window);
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
                win = gli_window_find_by_tag(data->window);
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

void glk_request_timer_events(glui32 millisecs)
{
    if (!pref_timersupport)
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

/* Work out how many milliseconds it has been since timing_start.
   If there is no timer, returns -1. */
static glsi32 gli_timer_request_since_start()
{
    struct timeval tv;

    if (!pref_timersupport)
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
