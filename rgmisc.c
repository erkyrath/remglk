/* rgmisc.c: Miscellaneous functions
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

data_supportcaps_t gli_supportcaps;

static unsigned char char_tolower_table[256];
static unsigned char char_toupper_table[256];

gidispatch_rock_t (*gli_register_obj)(void *obj, glui32 objclass) = NULL;
void (*gli_unregister_obj)(void *obj, glui32 objclass, gidispatch_rock_t objrock) = NULL;

gidispatch_rock_t (*gli_register_arr)(void *array, glui32 len, char *typecode) = NULL;
void (*gli_unregister_arr)(void *array, glui32 len, char *typecode, gidispatch_rock_t objrock) = NULL;

long (*gli_dispatch_locate_arr)(void *array, glui32 len, char *typecode, gidispatch_rock_t objrock, int *elemsizeref) = NULL;
gidispatch_rock_t (*gli_dispatch_restore_arr)(long bufkey, glui32 len, char *typecode, void **arrayref) = NULL;
        
/* Set up things. This is called from main(). */
void gli_initialize_misc(data_supportcaps_t *supportcaps)
{
    int ix;
    int res;

    gli_supportcaps = *supportcaps;
    
    /* Initialize the to-uppercase and to-lowercase tables. These should
        *not* be localized to a platform-native character set! They are
        intended to work on Latin-1 data, and the code below correctly
        sets up the tables for that character set. */
    
    for (ix=0; ix<256; ix++) {
        char_toupper_table[ix] = ix;
        char_tolower_table[ix] = ix;
    }
    for (ix=0; ix<256; ix++) {
        if (ix >= 'A' && ix <= 'Z') {
            res = ix + ('a' - 'A');
        }
        else if (ix >= 0xC0 && ix <= 0xDE && ix != 0xD7) {
            res = ix + 0x20;
        }
        else {
            res = 0;
        }
        if (res) {
            char_tolower_table[ix] = res;
            char_toupper_table[res] = ix;
        }
    }

}

void glk_exit()
{   
    gli_windows_update(NULL, TRUE);
    gli_streams_close_all();

    if (gli_debugger)
        gidebug_announce_cycle(gidebug_cycle_End);

    exit(0);
}

void glk_set_interrupt_handler(void (*func)(void))
{
    gli_interrupt_handler = func;
}

void glk_tick()
{
    /* Nothing to do here. */
}

void gidispatch_set_object_registry(
    gidispatch_rock_t (*regi)(void *obj, glui32 objclass), 
    void (*unregi)(void *obj, glui32 objclass, gidispatch_rock_t objrock))
{
    window_t *win;
    stream_t *str;
    fileref_t *fref;
    
    gli_register_obj = regi;
    gli_unregister_obj = unregi;
    
    if (gli_register_obj) {
        /* It's now necessary to go through all existing objects, and register
            them. */
        for (win = glk_window_iterate(NULL, NULL); 
            win;
            win = glk_window_iterate(win, NULL)) {
            win->disprock = (*gli_register_obj)(win, gidisp_Class_Window);
        }
        for (str = glk_stream_iterate(NULL, NULL); 
            str;
            str = glk_stream_iterate(str, NULL)) {
            str->disprock = (*gli_register_obj)(str, gidisp_Class_Stream);
        }
        for (fref = glk_fileref_iterate(NULL, NULL); 
            fref;
            fref = glk_fileref_iterate(fref, NULL)) {
            fref->disprock = (*gli_register_obj)(fref, gidisp_Class_Fileref);
        }
    }
}

void gidispatch_set_retained_registry(
    gidispatch_rock_t (*regi)(void *array, glui32 len, char *typecode), 
    void (*unregi)(void *array, glui32 len, char *typecode, 
        gidispatch_rock_t objrock))
{
    gli_register_arr = regi;
    gli_unregister_arr = unregi;
}

gidispatch_rock_t gidispatch_get_objrock(void *obj, glui32 objclass)
{
    switch (objclass) {
        case gidisp_Class_Window:
            return ((window_t *)obj)->disprock;
        case gidisp_Class_Stream:
            return ((stream_t *)obj)->disprock;
        case gidisp_Class_Fileref:
            return ((fileref_t *)obj)->disprock;
        default: {
            gidispatch_rock_t dummy;
            dummy.num = 0;
            return dummy;
        }
    }
}

void gidispatch_set_autorestore_registry(
    long (*locatearr)(void *array, glui32 len, char *typecode,
        gidispatch_rock_t objrock, int *elemsizeref),
    gidispatch_rock_t (*restorearr)(long bufkey, glui32 len,
        char *typecode, void **arrayref))
{
    gli_dispatch_locate_arr = locatearr;
    gli_dispatch_restore_arr = restorearr;
}

unsigned char glk_char_to_lower(unsigned char ch)
{
    return char_tolower_table[ch];
}

unsigned char glk_char_to_upper(unsigned char ch)
{
    return char_toupper_table[ch];
}

void gli_display_warning(char *msg)
{
    fprintf(stderr, "Glk library warning: %s\n", msg);
}

void gli_display_error(char *msg)
{
    if (pref_stderr) {
        fprintf(stderr, "%s\n", msg);
    }
    else {
        printf("{\"type\":\"error\", \"message\":");
        print_string_json(msg, stdout);
        printf("}\n");
    }
    printf("\n"); /* blank line after stanza */
    fflush(stdout);
    exit(1);
}

#ifdef NO_MEMMOVE

void *memmove(void *destp, void *srcp, int n)
{
    char *dest = (char *)destp;
    char *src = (char *)srcp;
    
    if (dest < src) {
        for (; n > 0; n--) {
            *dest = *src;
            dest++;
            src++;
        }
    }
    else if (dest > src) {
        src += n;
        dest += n;
        for (; n > 0; n--) {
            dest--;
            src--;
            *dest = *src;
        }
    }
    
    return destp;
}

#endif /* NO_MEMMOVE */
