/* rgdata_int.h: JSON data structure header with serialization internals
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include "glkstart.h"

typedef struct data_raw_struct data_raw_t;

struct glkunix_serialize_context_struct {
    FILE *file;
    glui32 count;
};

struct glkunix_unserialize_context_struct {
    data_raw_t *dat;
    struct glkunix_unserialize_context_struct *subctx;
};

extern void glkunix_serialize_object_root(FILE *file, struct glkunix_serialize_context_struct *ctx, glkunix_serialize_object_f func, void *rock);

extern int glkunix_unserialize_object_root(FILE *file, struct glkunix_unserialize_context_struct *ctx);
extern void glkunix_unserialize_object_root_finalize(struct glkunix_unserialize_context_struct *ctx);
