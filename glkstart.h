/* glkstart.h: Unix-specific header file for GlkTerm, CheapGlk, and XGlk
        (Unix implementations of the Glk API).
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

/* This header defines an interface that must be used by program linked
    with the various Unix Glk libraries -- at least, the three I wrote.
    (I encourage anyone writing a Unix Glk library to use this interface,
    but it's not part of the Glk spec.)
    
    Because Glk is *almost* perfectly portable, this interface *almost*
    doesn't have to exist. In practice, it's small.

    (Except for autosave. That makes everything complicated.)
*/

#ifndef GT_START_H
#define GT_START_H

/* We define our own TRUE and FALSE and NULL, because ANSI
    is a strange world. */
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL 0
#endif

#define glkunix_arg_End (0)
#define glkunix_arg_ValueFollows (1)
#define glkunix_arg_NoValue (2)
#define glkunix_arg_ValueCanFollow (3)
#define glkunix_arg_NumberValue (4)

typedef struct glkunix_argumentlist_struct {
    char *name;
    int argtype;
    char *desc;
} glkunix_argumentlist_t;

typedef struct glkunix_startup_struct {
    int argc;
    char **argv;
} glkunix_startup_t;

extern glkunix_argumentlist_t glkunix_arguments[];

/* defined in unixstrt.c */
extern int glkunix_startup_code(glkunix_startup_t *data);

/* This library offers the glkunix_fileref_get_filename() API
   (used by some VMs originally built for GarGlk). */
#define GLKUNIX_FILEREF_GET_FILENAME (1)

extern void glkunix_set_base_file(char *filename);
extern strid_t glkunix_stream_open_pathname_gen(char *pathname, 
    glui32 writemode, glui32 textmode, glui32 rock);
extern strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, 
    glui32 rock);
#ifdef GLKUNIX_FILEREF_GET_FILENAME
extern char *glkunix_fileref_get_filename(frefid_t fref);
#endif /* GLKUNIX_FILEREF_GET_FILENAME */

typedef struct glkunix_serialize_context_struct *glkunix_serialize_context_t;
typedef struct glkunix_unserialize_context_struct *glkunix_unserialize_context_t;
typedef int (*glkunix_serialize_object_f)(glkunix_serialize_context_t, void *);
typedef int (*glkunix_unserialize_object_f)(glkunix_unserialize_context_t, void *);

extern void glkunix_serialize_uint32(glkunix_serialize_context_t, char *, glui32);
extern void glkunix_serialize_object(glkunix_serialize_context_t, char *, glkunix_serialize_object_f, void *);
extern void glkunix_serialize_object_list(glkunix_serialize_context_t, char *, glkunix_serialize_object_f, int, size_t, void *);

extern int glkunix_unserialize_uint32(glkunix_unserialize_context_t, char *, glui32 *);
extern int glkunix_unserialize_struct(glkunix_unserialize_context_t, char *, glkunix_unserialize_context_t *);
extern int glkunix_unserialize_list(glkunix_unserialize_context_t, char *, glkunix_unserialize_context_t *, int *);
extern int glkunix_unserialize_list_entry(glkunix_unserialize_context_t, int, glkunix_unserialize_context_t *);
extern int glkunix_unserialize_object_list_entries(glkunix_unserialize_context_t, glkunix_unserialize_object_f, int, size_t, void *);

/* This library offers the hooks necessary for an interpreter to
   implement autosave. */
#define GLKUNIX_AUTOSAVE_FEATURES (1)

#ifdef GLKUNIX_AUTOSAVE_FEATURES

/* glkunix_library_state_struct is defined in rgdata_int.h. */
typedef struct glkunix_library_state_struct *glkunix_library_state_t;
/* glk_objrock_union is defined in gi_dispa.h. gidispatch_rock_t is another alias for this union; but I don't want this header to be dependent on that one, so we use glk_objrock_u below. */
typedef union glk_objrock_union glk_objrock_u;

extern void glkunix_save_library_state(strid_t file, strid_t omitstream, glkunix_serialize_object_f extra_state_func, void *extra_state_rock);
extern glkunix_library_state_t glkunix_load_library_state(strid_t file, glkunix_unserialize_object_f extra_state_func, void *extra_state_rock);
extern glui32 glkunix_update_from_library_state(glkunix_library_state_t state);
extern void glkunix_library_state_free(glkunix_library_state_t state);

extern glui32 glkunix_get_last_event_type(void);
extern glui32 glkunix_window_get_updatetag(winid_t win);
extern winid_t glkunix_window_find_by_updatetag(glui32 tag);
extern void glkunix_window_set_dispatch_rock(winid_t win, glk_objrock_u rock);
extern glui32 glkunix_stream_get_updatetag(strid_t str);
extern strid_t glkunix_stream_find_by_updatetag(glui32 tag);
extern void glkunix_stream_set_dispatch_rock(strid_t str, glk_objrock_u rock);
extern glui32 glkunix_fileref_get_updatetag(frefid_t fref);
extern frefid_t glkunix_fileref_find_by_updatetag(glui32 tag);
extern void glkunix_fileref_set_dispatch_rock(frefid_t fref, glk_objrock_u rock);

#endif /* GLKUNIX_AUTOSAVE_FEATURES */

#endif /* GT_START_H */

