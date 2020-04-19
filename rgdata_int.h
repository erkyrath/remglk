/* rgdata_int.h: JSON data structure header with serialization internals
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include "glkstart.h"

struct glkunix_serialize_context_struct {
    FILE *file;
    glui32 count;
};

extern void glkunix_serialize_object_root(FILE *file, struct glkunix_serialize_context_struct *ctx, glkunix_serialize_object_f func, void *rock);
