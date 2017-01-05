/* gtw_graph.h: The graphics window header
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

typedef struct window_graphics_struct {
    window_t *owner;
    
    data_specialspan_t **content;
    long numcontent;
    long contentsize;

    long updatemark;
    
    int graphwidth, graphheight;
} window_graphics_t;

extern window_graphics_t *win_graphics_create(window_t *win);
extern void win_graphics_destroy(window_graphics_t *dwin);
extern void win_graphics_rearrange(window_t *win, grect_t *box, data_metrics_t *metrics);
extern void win_graphics_redraw(window_t *win);
extern void win_graphics_putspecial(window_t *win, data_specialspan_t *span);
extern data_content_t *win_graphics_update(window_t *win);
extern void win_graphics_clear(window_t *win);
extern void win_graphics_trim_buffer(window_t *win);
