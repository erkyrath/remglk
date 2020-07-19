/* remglk.h: Private header file
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#ifndef REMGLK_H
#define REMGLK_H

#include "gi_dispa.h"
#include "gi_debug.h"

#define LIBRARY_VERSION "0.2.6"

/* We define our own TRUE and FALSE and NULL, because ANSI
    is a strange world. */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

/* This macro is called whenever the library code catches an error
    or illegal operation from the game program. */

#define gli_strict_warning(msg)   \
    (gli_display_warning(msg)) 

#define gli_fatal_error(msg)   \
    (gli_display_error(msg))

/* Some useful type declarations. */

typedef struct grect_struct {
    int left, top;
    int right, bottom;
} grect_t;

#define grect_set_from_size(boxref, wid, hgt)   \
    ((boxref)->left = 0, (boxref)->top = 0,     \
     (boxref)->right = (wid), (boxref)->bottom = (hgt))

typedef struct data_raw_struct data_raw_t;
typedef struct data_metrics_struct data_metrics_t;
typedef struct data_content_struct data_content_t;
typedef struct data_specialreq_struct data_specialreq_t;
typedef struct data_tempbufinfo_struct data_tempbufinfo_t;
typedef struct data_supportcaps_struct data_supportcaps_t;

typedef struct glk_window_struct window_t;
typedef struct glk_stream_struct stream_t;
typedef struct glk_fileref_struct fileref_t;

#define MAGIC_WINDOW_NUM (9826)
#define MAGIC_STREAM_NUM (8269)
#define MAGIC_FILEREF_NUM (6982)

struct glk_window_struct {
    glui32 magicnum;
    glui32 rock;
    glui32 type;
    glui32 updatetag; /* numeric tag for the window in output and autosave */
    
    grect_t bbox; /* content rectangle, excluding borders */
    window_t *parent; /* pair window which contains this one */
    void *data; /* one of the window_*_t structures */
    
    stream_t *str; /* the window stream. */
    stream_t *echostr; /* the window's echo stream, if any. */

    glui32 inputgen;    
    int line_request;
    int line_request_uni;
    int char_request;
    int char_request_uni;
    int hyperlink_request;

    int echo_line_input; /* applies to future line inputs, not the current */
    glui32 terminate_line_input; /* ditto; this is a bitmask of flags */

    glui32 style;
    glui32 hyperlink;
    
    /* only used in a temporary library_state, while deserializing. */
    data_tempbufinfo_t *tempbufinfo;
    
    gidispatch_rock_t disprock;
    window_t *next, *prev; /* in the big linked list of windows */
};

#define strtype_File (1)
#define strtype_Window (2)
#define strtype_Memory (3)
#define strtype_Resource (4)

struct glk_stream_struct {
    glui32 magicnum;
    glui32 rock;
    glui32 updatetag; /* numeric tag for the stream in autosave */

    int type; /* file, window, or memory stream */
    int unicode; /* one-byte or four-byte chars? Not meaningful for windows */
    
    glui32 readcount, writecount;
    int readable, writable;
    
    /* for strtype_Window */
    window_t *win;
    
    /* for strtype_File */
    FILE *file; 
    glui32 lastop; /* 0, filemode_Write, or filemode_Read */
    char *filename; /* only needed for autosave */
    char *modestr; /* only needed for autosave */
    
    /* for strtype_File, strtype_Resource */
    int isbinary;

    /* for strtype_Resource */
    glui32 fileresnum; /* only needed for autosave */

    /* for strtype_Memory and strtype_Resource. Separate pointers for 
       one-byte and four-byte streams */
    /* note that buf/ubuf point to memory outside the library. Usually it's owned by the dispatch layer. The other pointers point within buf/ubuf. */
    unsigned char *buf;
    unsigned char *bufptr;
    unsigned char *bufend;
    unsigned char *bufeof;
    glui32 *ubuf;
    glui32 *ubufptr;
    glui32 *ubufend;
    glui32 *ubufeof;
    glui32 buflen;
    gidispatch_rock_t arrayrock;

    /* only used in a temporary library_state, while deserializing. */
    data_tempbufinfo_t *tempbufinfo;

    gidispatch_rock_t disprock;
    stream_t *next, *prev; /* in the big linked list of streams */
};

struct glk_fileref_struct {
    glui32 magicnum;
    glui32 rock;
    glui32 updatetag; /* numeric tag for the fileref in autosave */

    char *filename;
    int filetype;
    int textmode;

    gidispatch_rock_t disprock;
    fileref_t *next, *prev; /* in the big linked list of filerefs */
};

/* A few global variables */

extern data_supportcaps_t gli_supportcaps;
extern window_t *gli_rootwin;
extern window_t *gli_focuswin;
extern stream_t *gli_currentstr;
extern void (*gli_interrupt_handler)(void);

/* The following typedefs are copied from cheapglk.h. They support the
   tables declared in cgunigen.c. */

typedef glui32 gli_case_block_t[2]; /* upper, lower */
/* If both are 0xFFFFFFFF, you have to look at the special-case table */

typedef glui32 gli_case_special_t[3]; /* upper, lower, title */
/* Each of these points to a subarray of the unigen_special_array
   (in cgunicode.c). In that subarray, element zero is the length,
   and that's followed by length unicode values. */

typedef glui32 gli_decomp_block_t[2]; /* count, position */
/* The position points to a subarray of the unigen_decomp_array.
   If the count is zero, there is no decomposition. */


extern gidispatch_rock_t (*gli_register_obj)(void *obj, glui32 objclass);
extern void (*gli_unregister_obj)(void *obj, glui32 objclass, gidispatch_rock_t objrock);
extern gidispatch_rock_t (*gli_register_arr)(void *array, glui32 len, char *typecode);
extern void (*gli_unregister_arr)(void *array, glui32 len, char *typecode, gidispatch_rock_t objrock);
extern long (*gli_dispatch_locate_arr)(void *array, glui32 len, char *typecode, gidispatch_rock_t objrock, int *elemsizeref);
extern gidispatch_rock_t (*gli_dispatch_restore_arr)(long bufkey, glui32 len, char *typecode, void **arrayref);

extern int pref_stderr;
extern int pref_singleturn;
extern char *pref_resourceurl;

#if GIDEBUG_LIBRARY_SUPPORT
/* Has the user requested debug support? */
extern int gli_debugger;
#else /* GIDEBUG_LIBRARY_SUPPORT */
#define gli_debugger (0)
#endif /* GIDEBUG_LIBRARY_SUPPORT */

/* Declarations of library internal functions. */

extern void gli_initialize_misc(data_supportcaps_t *supportcaps);

extern void gli_msgline_warning(char *msg);
extern void gli_msgline_error(char *msg);
extern void gli_msgline(char *msg);
extern void gli_msgline_redraw(void);

extern int gli_msgin_getline(char *prompt, char *buf, int maxlen, int *length);
extern int gli_msgin_getchar(char *prompt, int hilite);

extern void gli_putchar_utf8(glui32 val, FILE *fl);
extern glui32 gli_parse_utf8(unsigned char *buf, glui32 buflen,
    glui32 *out, glui32 outlen);
extern int gli_encode_utf8(glui32 val, char *buf, int len);

extern void gli_initialize_events(void);
extern void gli_event_store(glui32 type, window_t *win, glui32 val1, glui32 val2);
extern void gli_set_last_event_type(glui32 type);
extern int gli_timer_need_update(glui32 *msec);
extern glui32 gli_timer_get_timing_msec(void);
extern void gli_select_metrics(data_metrics_t *metrics, data_supportcaps_t *supportcaps);
extern char *gli_select_specialrequest(data_specialreq_t *special);
extern void gli_select_imaginary(void);

extern void gli_initialize_windows(void);
extern void gli_fast_exit(void);
extern void gli_display_warning(char *msg);
extern void gli_display_error(char *msg);
extern glui32 gli_window_current_generation(void);
extern winid_t glkunix_window_find_by_updatetag(glui32 tag); /* see glkstart.h */
extern window_t *gli_new_window(glui32 type, glui32 rock);
extern window_t *gli_window_alloc_inactive(void);
extern void gli_window_dealloc_inactive(window_t *win);
extern void gli_delete_window(window_t *win);
extern int gli_windows_update_from_state(window_t **list, int count, window_t *rootwin, glui32 gen);
extern window_t *gli_window_iterate_treeorder(window_t *win);
extern void gli_window_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics);
extern void gli_windows_update(data_specialreq_t *special, int newgeneration);
extern void gli_windows_refresh(glui32 fromgen);
extern void gli_windows_metrics_change(data_metrics_t *newmetrics);
extern data_metrics_t *gli_windows_get_metrics(void);
extern void gli_windows_update_metrics(data_metrics_t *newmetrics);
extern void gli_windows_trim_buffers(void);
extern void gli_window_put_char(window_t *win, glui32 ch);
extern void gli_windows_unechostream(stream_t *str);
extern void gli_window_prepare_input(window_t *win, glui32 *buf, glui32 len);
extern void gli_window_accept_line(window_t *win);
extern void gli_print_spaces(int len);

extern void gcmd_win_change_focus(window_t *win, glui32 arg);
extern void gcmd_win_refresh(window_t *win, glui32 arg);

extern void gli_initialize_streams(void);
extern stream_t *gli_new_stream(int type, int readable, int writable, 
    glui32 rock);
extern stream_t *gli_stream_alloc_inactive(void);
extern void gli_stream_dealloc_inactive(stream_t *str);
extern void gli_delete_stream(stream_t *str);
extern int gli_streams_update_from_state(stream_t **list, int count, stream_t *currentstr);
extern stream_t *gli_stream_open_window(window_t *win);
extern strid_t gli_stream_open_pathname(char *pathname, int writemode, 
    int textmode, glui32 rock);
extern void gli_stream_set_current(stream_t *str);
extern void gli_stream_fill_result(stream_t *str, 
    stream_result_t *result);
extern void gli_stream_echo_line(stream_t *str, char *buf, glui32 len);
extern void gli_stream_echo_line_uni(stream_t *str, glui32 *buf, glui32 len);
extern void gli_streams_close_all(void);

extern void gli_initialize_filerefs(void);
extern fileref_t *gli_new_fileref(char *filename, glui32 usage, 
    glui32 rock);
extern fileref_t *gli_fileref_alloc_inactive(void);
extern void gli_fileref_dealloc_inactive(fileref_t *fref);
extern void gli_delete_fileref(fileref_t *fref);
extern int gli_filerefs_update_from_state(fileref_t **list, int count);

/* A macro that I can't think of anywhere else to put it. */

#define gli_event_clearevent(evp)  \
    ((evp)->type = evtype_None,    \
    (evp)->win = NULL,    \
    (evp)->val1 = 0,   \
    (evp)->val2 = 0)

/* A macro which reads and decodes one character of UTF-8. Needs no
   explanation, I'm sure.

   Oh, okay. The character will be written to *chptr (so pass in "&ch",
   where ch is a glui32 variable). eofcond should be a condition to
   evaluate end-of-stream -- true if no more characters are readable.
   nextch is a function which reads the next character; this is invoked
   exactly as many times as necessary.

   val0, val1, val2, val3 should be glui32 scratch variables. The macro
   needs these. Just define them, you don't need to pay attention to them
   otherwise.

   The macro itself evaluates to true if ch was successfully set, or
   false if something went wrong. (Not enough characters, or an
   invalid byte sequence.)

   This is not the worst macro I've ever written, but I forget what the
   other one was.
*/

#define UTF8_DECODE_INLINE(chptr, eofcond, nextch, val0, val1, val2, val3)  ( \
    (eofcond ? 0 : ( \
        (((val0=nextch) < 0x80) ? (*chptr=val0, 1) : ( \
            (eofcond ? 0 : ( \
                (((val1=nextch) & 0xC0) != 0x80) ? 0 : ( \
                    (((val0 & 0xE0) == 0xC0) ? (*chptr=((val0 & 0x1F) << 6) | (val1 & 0x3F), 1) : ( \
                        (eofcond ? 0 : ( \
                            (((val2=nextch) & 0xC0) != 0x80) ? 0 : ( \
                                (((val0 & 0xF0) == 0xE0) ? (*chptr=(((val0 & 0xF)<<12)  & 0x0000F000) | (((val1 & 0x3F)<<6) & 0x00000FC0) | (((val2 & 0x3F))    & 0x0000003F), 1) : ( \
                                    (((val0 & 0xF0) != 0xF0 || eofcond) ? 0 : (\
                                        (((val3=nextch) & 0xC0) != 0x80) ? 0 : (*chptr=(((val0 & 0x7)<<18)   & 0x1C0000) | (((val1 & 0x3F)<<12) & 0x03F000) | (((val2 & 0x3F)<<6)  & 0x000FC0) | (((val3 & 0x3F))     & 0x00003F), 1) \
                                        )) \
                                    )) \
                                )) \
                            )) \
                        )) \
                )) \
            )) \
        )) \
    )

#ifdef NO_MEMMOVE
    extern void *memmove(void *dest, void *src, int n);
#endif /* NO_MEMMOVE */

#endif /* REMGLK_H */
