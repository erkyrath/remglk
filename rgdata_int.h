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

struct glkunix_unserialize_context_struct {
    data_raw_t *dat;
    struct glkunix_unserialize_context_struct *subctx;
};

struct glkunix_library_state_struct {
    data_metrics_t *metrics;

    window_t **windowlist;
    int windowcount;
    stream_t **streamlist;
    int streamcount;
    fileref_t **filereflist;
    int filerefcount;

};

extern glkunix_library_state_t glkunix_library_state_alloc(void);
extern void glkunix_library_state_free(glkunix_library_state_t state);

extern void glkunix_serialize_object_root(FILE *file, struct glkunix_serialize_context_struct *ctx, glkunix_serialize_object_f func, void *rock);

extern int glkunix_unserialize_object_root(FILE *file, struct glkunix_unserialize_context_struct *ctx);
extern void glkunix_unserialize_object_root_finalize(struct glkunix_unserialize_context_struct *ctx);
