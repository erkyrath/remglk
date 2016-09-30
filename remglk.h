/* remglk.h: Private header file
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#ifndef REMGLK_H
#define REMGLK_H

#include "gi_dispa.h"

#define LIBRARY_VERSION "0.2.1"

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

typedef struct data_metrics_struct data_metrics_t;
typedef struct data_content_struct data_content_t;
typedef struct data_specialreq_struct data_specialreq_t;

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
    glui32 updatetag; /* numeric tag for the window in output */
    
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

    int echo_line_input; /* applies to future line inputs, not the current */
    glui32 terminate_line_input; /* ditto; this is a bitmask of flags */

    glui32 style;
    
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

    int type; /* file, window, or memory stream */
    int unicode; /* one-byte or four-byte chars? Not meaningful for windows */
    
    glui32 readcount, writecount;
    int readable, writable;
    
    /* for strtype_Window */
    window_t *win;
    
    /* for strtype_File */
    FILE *file; 
    glui32 lastop; /* 0, filemode_Write, or filemode_Read */
    
    /* for strtype_Resource */
    int isbinary;

    /* for strtype_Memory and strtype_Resource. Separate pointers for 
       one-byte and four-byte streams */
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

    gidispatch_rock_t disprock;
    stream_t *next, *prev; /* in the big linked list of streams */
};

struct glk_fileref_struct {
    glui32 magicnum;
    glui32 rock;

    char *filename;
    int filetype;
    int textmode;

    gidispatch_rock_t disprock;
    fileref_t *next, *prev; /* in the big linked list of filerefs */
};

/* A few global variables */

extern window_t *gli_rootwin;
extern window_t *gli_focuswin;
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
extern void (*gli_unregister_arr)(void *array, glui32 len, char *typecode, 
    gidispatch_rock_t objrock);

extern int pref_stderr;
extern int pref_printversion;
extern int pref_screenwidth;
extern int pref_screenheight;

/* Declarations of library internal functions. */

extern void gli_initialize_misc(void);

extern void gli_msgline_warning(char *msg);
extern void gli_msgline_error(char *msg);
extern void gli_msgline(char *msg);
extern void gli_msgline_redraw(void);

extern int gli_msgin_getline(char *prompt, char *buf, int maxlen, int *length);
extern int gli_msgin_getchar(char *prompt, int hilite);

extern void gli_putchar_utf8(glui32 val, FILE *fl);
extern glui32 gli_parse_utf8(unsigned char *buf, glui32 buflen,
    glui32 *out, glui32 outlen);

extern void gli_initialize_events(void);
extern void gli_event_store(glui32 type, window_t *win, glui32 val1, glui32 val2);

extern void gli_initialize_windows(data_metrics_t *metrics);
extern void gli_fast_exit(void);
extern void gli_display_warning(char *msg);
extern void gli_display_error(char *msg);
extern glui32 gli_window_current_generation(void);
extern window_t *gli_window_find_by_tag(glui32 tag);
extern window_t *gli_new_window(glui32 type, glui32 rock);
extern void gli_delete_window(window_t *win);
extern window_t *gli_window_iterate_treeorder(window_t *win);
extern void gli_window_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics);
extern void gli_windows_update(data_specialreq_t *special, int newgeneration);
extern void gli_windows_refresh(glui32 fromgen);
extern void gli_windows_metrics_change(data_metrics_t *newmetrics);
extern void gli_windows_trim_buffers(void);
extern void gli_window_put_char(window_t *win, glui32 ch);
extern void gli_windows_unechostream(stream_t *str);
extern void gli_window_prepare_input(window_t *win, glui32 *buf, glui32 len);
extern void gli_window_accept_line(window_t *win);
extern void gli_print_spaces(int len);

extern void gcmd_win_change_focus(window_t *win, glui32 arg);
extern void gcmd_win_refresh(window_t *win, glui32 arg);

extern stream_t *gli_new_stream(int type, int readable, int writable, 
    glui32 rock);
extern void gli_delete_stream(stream_t *str);
extern stream_t *gli_stream_open_window(window_t *win);
extern strid_t gli_stream_open_pathname(char *pathname, int writemode, 
    int textmode, glui32 rock);
extern void gli_stream_set_current(stream_t *str);
extern void gli_stream_fill_result(stream_t *str, 
    stream_result_t *result);
extern void gli_stream_echo_line(stream_t *str, char *buf, glui32 len);
extern void gli_stream_echo_line_uni(stream_t *str, glui32 *buf, glui32 len);
extern void gli_streams_close_all(void);

extern fileref_t *gli_new_fileref(char *filename, glui32 usage, 
    glui32 rock);
extern void gli_delete_fileref(fileref_t *fref);

/* A macro that I can't think of anywhere else to put it. */

#define gli_event_clearevent(evp)  \
    ((evp)->type = evtype_None,    \
    (evp)->win = NULL,    \
    (evp)->val1 = 0,   \
    (evp)->val2 = 0)

#ifdef NO_MEMMOVE
    extern void *memmove(void *dest, void *src, int n);
#endif /* NO_MEMMOVE */

#endif /* REMGLK_H */
