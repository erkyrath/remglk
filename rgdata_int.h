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
    glui32 generation;
    data_metrics_t *metrics;
    data_supportcaps_t *supportcaps;

    window_t **windowlist;
    int windowcount;
    stream_t **streamlist;
    int streamcount;
    fileref_t **filereflist;
    int filerefcount;

    glui32 timerinterval;

    window_t *rootwin;
    stream_t *currentstr;
};

extern glkunix_library_state_t glkunix_library_state_alloc(void);
extern void glkunix_library_state_free(glkunix_library_state_t state);

/* Some serialize/unserialize calls that are used by the library but not exported to game/interpreter code. */

extern void glkunix_serialize_object_root(FILE *file, struct glkunix_serialize_context_struct *ctx, glkunix_serialize_object_f func, void *rock);

extern int glkunix_unserialize_int(glkunix_unserialize_context_t, char *, int *);
extern int glkunix_unserialize_long(glkunix_unserialize_context_t, char *, long *);
extern int glkunix_unserialize_latin1_string(glkunix_unserialize_context_t, char *, char **);
extern int glkunix_unserialize_len_bytes(glkunix_unserialize_context_t, char *, unsigned char **, long *);
extern int glkunix_unserialize_len_unicode(glkunix_unserialize_context_t, char *, glui32 **, long *);

extern int glkunix_unserialize_uint32_list_entry(glkunix_unserialize_context_t, int, glui32 *);

extern int glkunix_unserialize_object_root(FILE *file, struct glkunix_unserialize_context_struct *ctx);
extern void glkunix_unserialize_object_root_finalize(struct glkunix_unserialize_context_struct *ctx);
