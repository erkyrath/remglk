/* gtw_buf.h: The buffer window header
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

/* One style/link run */
typedef struct tbrun_struct {
    short style;
    glui32 hyperlink;
    long pos;
    long specialnum;
} tbrun_t;

typedef struct window_textbuffer_struct {
    window_t *owner;
    
    glui32 *chars;
    long numchars;
    long charssize;

    data_specialspan_t **specials;
    long numspecials;
    long specialssize;
    
    int width, height;
    
    long updatemark;
    
    tbrun_t *runs; /* There is always at least one run. */
    long numruns;
    long runssize;

    /* The following are meaningful only for the current line input request. */
    void *inbuf; /* char* or glui32*, depending on inunicode. */
    glui32 incurpos;
    int inunicode;
    int inecho;
    glui32 intermkeys;
    int inmax;
    glui32 origstyle;
    glui32 orighyperlink;
    gidispatch_rock_t inarrayrock;
} window_textbuffer_t;

extern window_textbuffer_t *win_textbuffer_create(window_t *win);
extern void win_textbuffer_destroy(window_textbuffer_t *dwin);
extern void win_textbuffer_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics);
extern void win_textbuffer_redraw(window_t *win);
extern data_content_t *win_textbuffer_update(window_t *win);
extern void win_textbuffer_putchar(window_t *win, glui32 ch);
extern void win_textbuffer_putspecial(window_t *win, data_specialspan_t *special);
extern void win_textbuffer_clear(window_t *win);
extern void win_textbuffer_trim_buffer(window_t *win);
extern void win_textbuffer_set_paging(window_t *win, int forcetoend);
extern void win_textbuffer_init_line(window_t *win, void *buf, int unicode, int maxlen, int initlen);
extern void win_textbuffer_accept_line(window_t *win);
extern void win_textbuffer_prepare_input(window_t *win, glui32 *buf, glui32 len);
extern void win_textbuffer_accept_line(window_t *win);
extern void win_textbuffer_cancel_line(window_t *win, event_t *ev);

