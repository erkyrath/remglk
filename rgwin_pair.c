/* rgwin_pair.c: The pair window type
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include "glk.h"
#include "remglk.h"
#include "rgwin_pair.h"

window_pair_t *win_pair_create(window_t *win, glui32 method, window_t *key, 
    glui32 size)
{
    window_pair_t *dwin = (window_pair_t *)malloc(sizeof(window_pair_t));
    dwin->owner = win;
    
    dwin->dir = method & winmethod_DirMask; 
    dwin->division = method & winmethod_DivisionMask;
    dwin->hasborder = ((method & winmethod_BorderMask) == winmethod_Border);
    dwin->key = key;
    dwin->keydamage = FALSE;
    dwin->size = size;
    
    dwin->vertical = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Right);
    dwin->backward = (dwin->dir == winmethod_Left || dwin->dir == winmethod_Above);
    
    dwin->child1 = NULL;
    dwin->child2 = NULL;
    
    return dwin;
}

void win_pair_destroy(window_pair_t *dwin)
{
    dwin->owner = NULL;
    /* We leave the children untouched, because gli_window_close takes care
        of that if it's desired. */
    dwin->child1 = NULL;
    dwin->child2 = NULL;
    dwin->key = NULL;
    free(dwin);
}

void win_pair_rearrange(window_t *win, grect_t *box)
{
    window_pair_t *dwin = win->data;
    grect_t box1, box2;
    int min, diff, split, splitwid, max;
    window_t *key;
    window_t *ch1, *ch2;

    win->bbox = *box;
    /*dwin->flat = FALSE;*/

    if (dwin->vertical) {
        min = win->bbox.left;
        max = win->bbox.right;
    }
    else {
        min = win->bbox.top;
        max = win->bbox.bottom;
    }
    diff = max-min;
    
    /* We now figure split. The window attributes control this, unless
       the pref_override_window_borders option is set. */
    if (pref_override_window_borders) {
        if (pref_window_borders)
            splitwid = 1;
        else
            splitwid = 0;
    }
    else {
        if (dwin->hasborder)
            splitwid = 1;
        else
            splitwid = 0;
    }
    
    switch (dwin->division) {
        case winmethod_Proportional:
            split = (diff * dwin->size) / 100;
            break;
        case winmethod_Fixed:
            /* Keeping track of the key may seem silly, since we don't really
                use it when all sizes are measured in characters. But it's
                good to know when it's invalid, so that the split can be set
                to zero. It would suck if invalid keys seemed to work in
                RemGlk but not in GUI Glk libraries. */
            key = dwin->key;
            if (!key) {
                split = 0;
            }
            else {
                switch (key->type) {
                    case wintype_TextBuffer:
                    case wintype_TextGrid:
                        split = dwin->size;
                        break;
                    default:
                        split = 0;
                        break;
                }
            }
            break;
        default:
            split = diff / 2;
            break;
    }
    
    if (!dwin->backward) {
        split = max-split-splitwid;
    }
    else {
        split = min+split;
    }

    if (min >= max) {
        split = min;
    }
    else {
      if (split < min)
          split = min;
      else if (split > max-splitwid)
          split = max-splitwid;
    }

    if (dwin->vertical) {
        dwin->splitpos = split;
        dwin->splitwidth = splitwid;
        box1.left = win->bbox.left;
        box1.right = dwin->splitpos;
        box2.left = box1.right + dwin->splitwidth;
        box2.right = win->bbox.right;
        box1.top = win->bbox.top;
        box1.bottom = win->bbox.bottom;
        box2.top = win->bbox.top;
        box2.bottom = win->bbox.bottom;
        if (!dwin->backward) {
            ch1 = dwin->child1;
            ch2 = dwin->child2;
        }
        else {
            ch1 = dwin->child2;
            ch2 = dwin->child1;
        }
    }
    else {
        dwin->splitpos = split;
        dwin->splitwidth = splitwid;
        box1.top = win->bbox.top;
        box1.bottom = dwin->splitpos;
        box2.top = box1.bottom + dwin->splitwidth;
        box2.bottom = win->bbox.bottom;
        box1.left = win->bbox.left;
        box1.right = win->bbox.right;
        box2.left = win->bbox.left;
        box2.right = win->bbox.right;
        if (!dwin->backward) {
            ch1 = dwin->child1;
            ch2 = dwin->child2;
        }
        else {
            ch1 = dwin->child2;
            ch2 = dwin->child1;
        }
    }
    
    gli_window_rearrange(ch1, &box1);
    gli_window_rearrange(ch2, &box2);
}

