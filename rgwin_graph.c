/* rgwin_graph.c: The graphics window type
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
#include "rgwin_graph.h"

window_graphics_t *win_graphics_create(window_t *win)
{
    window_graphics_t *dwin = (window_graphics_t *)malloc(sizeof(window_graphics_t));
    dwin->owner = win;
    
    dwin->numcontent = 0;
    dwin->contentsize = 4;
    dwin->content = (data_specialspan_t **)malloc(dwin->contentsize * sizeof(data_specialspan_t *));
    

    return dwin;
}
