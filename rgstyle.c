/* gtstyle.c: Style formatting hints.
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "rgwin_grid.h"
#include "rgwin_buf.h"

/* This version of the library doesn't accept style hints. */

void glk_stylehint_set(glui32 wintype, glui32 styl, glui32 hint, 
    glsi32 val)
{
}

void glk_stylehint_clear(glui32 wintype, glui32 styl, glui32 hint)
{
}

glui32 glk_style_distinguish(window_t *win, glui32 styl1, glui32 styl2)
{
    if (!win) {
        gli_strict_warning("style_distinguish: invalid ref");
        return FALSE;
    }
    
    if (styl1 >= style_NUMSTYLES || styl2 >= style_NUMSTYLES)
        return FALSE;
    
    /* ### */
    
    return FALSE;
}

glui32 glk_style_measure(window_t *win, glui32 styl, glui32 hint, 
    glui32 *result)
{
    glui32 dummy;

    if (!win) {
        gli_strict_warning("style_measure: invalid ref");
        return FALSE;
    }
    
    if (styl >= style_NUMSTYLES || hint >= stylehint_NUMHINTS)
        return FALSE;
    
    if (!result)
        result = &dummy;

    /* ### */    
    
    return FALSE;
}
