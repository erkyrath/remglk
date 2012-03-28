/* gtmessag.c: The message line at the bottom of the screen
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"

/* ### stubs, will move somewhere else */

void gli_msgline_warning(char *msg)
{
    fprintf(stderr, "Glk library error: %s\n", msg);
}

void gli_msgline_error(char *msg)
{
    fprintf(stderr, "%s\n", msg); /*###*/

    exit(1); /* ### or something */
}
