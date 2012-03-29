/* gtw_grid.h: The grid window header
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

/* One line of the window. */
typedef struct tgline_struct {
    int size; /* this is the allocated size; only width is valid */
    char *chars;
    unsigned char *attrs;
    int dirtybeg, dirtyend; /* characters [dirtybeg, dirtyend) need to be redrawn */
} tgline_t;

typedef struct window_textgrid_struct {
    window_t *owner;
    
    int width, height;
    tgline_t *lines;
    int linessize; /* this is the allocated size of the lines array;
        only the first height entries are valid. */
    
    int curx, cury; /* the window cursor position */
    
    int dirtybeg, dirtyend; /* lines [dirtybeg, dirtyend) need to be redrawn */
    
    /* for line input */
    void *inbuf; /* char* or glui32*, depending on inunicode. */
    int inunicode;
    int inorgx, inorgy;
    glui32 intermkeys;
    int inoriglen, inmax;
    int incurs, inlen;
    glui32 origstyle;
    gidispatch_rock_t inarrayrock;
} window_textgrid_t;

extern window_textgrid_t *win_textgrid_create(window_t *win);
extern void win_textgrid_destroy(window_textgrid_t *dwin);
extern void win_textgrid_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics);
extern void win_textgrid_redraw(window_t *win);
extern void win_textgrid_update(window_t *win);
extern void win_textgrid_putchar(window_t *win, glui32 ch);
extern void win_textgrid_clear(window_t *win);
extern void win_textgrid_move_cursor(window_t *win, int xpos, int ypos);
extern void win_textgrid_place_cursor(window_t *win, int *xpos, int *ypos);
extern void win_textgrid_init_line(window_t *win, void *buf, int unicode, int maxlen, int initlen);
extern void win_textgrid_cancel_line(window_t *win, event_t *ev);
