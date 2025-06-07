/* rgstream.c: Stream objects
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"
#include "gi_blorb.h"
#include "rgdata.h"

/* This implements pretty much what any Glk implementation needs for 
    stream stuff. Memory streams, file streams (using stdio functions), 
    and window streams (which print through window functions in other
    files.) A different implementation would change the window stream
    stuff, but not file or memory streams. (Unless you're on a 
    wacky platform like the Mac and want to change stdio to native file 
    functions.) 
*/

/* Used to generate stream updatetag values. */
static glui32 tagcounter = 0;

static stream_t *gli_streamlist = NULL; /* linked list of all streams */
stream_t *gli_currentstr = NULL; /* the current output stream */

void gli_initialize_streams()
{
    tagcounter = (random() % 15) + 32;
}

stream_t *glkunix_stream_find_by_updatetag(glui32 tag)
{
    stream_t *str;
    for (str=gli_streamlist; str; str=str->next) {
        if (str->updatetag == tag)
            return str;
    }
    return NULL;
}

void glkunix_stream_set_dispatch_rock(strid_t str, gidispatch_rock_t rock)
{
    str->disprock = rock;
}

glui32 glkunix_stream_get_updatetag(strid_t str)
{
    return str->updatetag;
}

stream_t *gli_new_stream(int type, int readable, int writable, 
    glui32 rock)
{
    stream_t *str = (stream_t *)malloc(sizeof(stream_t));
    if (!str)
        return NULL;
    
    str->magicnum = MAGIC_STREAM_NUM;
    str->type = type;
    str->rock = rock;
    str->updatetag = tagcounter;
    tagcounter += 5;

    str->unicode = FALSE;
    str->isbinary = FALSE;
    
    str->win = NULL;
    str->file = NULL;
    str->filename = NULL;
    str->modestr = NULL;
    str->fileresnum = 0;
    str->lastop = 0;
    str->buf = NULL;
    str->bufptr = NULL;
    str->bufend = NULL;
    str->bufeof = NULL;
    str->ubuf = NULL;
    str->ubufptr = NULL;
    str->ubufend = NULL;
    str->ubufeof = NULL;
    str->buflen = 0;
    
    str->readcount = 0;
    str->writecount = 0;
    str->readable = readable;
    str->writable = writable;

    str->tempbufinfo = NULL;
    
    str->prev = NULL;
    str->next = gli_streamlist;
    gli_streamlist = str;
    if (str->next) {
        str->next->prev = str;
    }
    
    if (gli_register_obj)
        str->disprock = (*gli_register_obj)(str, gidisp_Class_Stream);
    else
        str->disprock.ptr = NULL;
    
    return str;
}

stream_t *gli_stream_alloc_inactive()
{
    stream_t *str = (stream_t *)malloc(sizeof(stream_t));
    if (!str)
        return NULL;
    
    str->magicnum = MAGIC_STREAM_NUM;
    str->type = 0;
    str->rock = 0;
    str->updatetag = 0;

    str->unicode = FALSE;
    str->isbinary = FALSE;
    
    str->win = NULL;
    str->file = NULL;
    str->filename = NULL;
    str->modestr = NULL;
    str->fileresnum = 0;
    str->lastop = 0;
    str->buf = NULL;
    str->bufptr = NULL;
    str->bufend = NULL;
    str->bufeof = NULL;
    str->ubuf = NULL;
    str->ubufptr = NULL;
    str->ubufend = NULL;
    str->ubufeof = NULL;
    str->buflen = 0;
    
    str->readcount = 0;
    str->writecount = 0;
    str->readable = FALSE;
    str->writable = FALSE;

    str->tempbufinfo = NULL;
    
    str->prev = NULL;
    str->next = NULL;
    
    return str;
}

void gli_stream_dealloc_inactive(stream_t *str)
{
    if (str->filename) {
        free(str->filename);
        str->filename = NULL;
    }
    if (str->modestr) {
        free(str->modestr);
        str->modestr = NULL;
    }
    
    if (str->tempbufinfo) {
        data_tempbufinfo_free(str->tempbufinfo);
        str->tempbufinfo = NULL;
    }
    
    free(str);
}

void gli_delete_stream(stream_t *str)
{
    stream_t *prev, *next;
    
    if (str == gli_currentstr) {
        gli_currentstr = NULL;
    }
    
    gli_windows_unechostream(str);
    
    str->magicnum = 0;

    switch (str->type) {
        case strtype_Window:
            /* nothing necessary; the window is already being closed */
            break;
        case strtype_Memory: 
            if (gli_unregister_arr) {
                /* This could be a char array or a glui32 array. */
                char *typedesc = (str->unicode ? "&+#!Iu" : "&+#!Cn");
                void *buf = (str->unicode ? (void*)str->ubuf : (void*)str->buf);
                (*gli_unregister_arr)(buf, str->buflen, typedesc,
                    str->arrayrock);
            }
            break;
        case strtype_Resource: 
            /* nothing necessary; the array belongs to gi_blorb.c. */
            break;
        case strtype_File:
            /* close the FILE */
            fclose(str->file);
            str->file = NULL;
            str->lastop = 0;
            break;
    }

    if (str->filename) {
        free(str->filename);
        str->filename = NULL;
    }
    if (str->modestr) {
        free(str->modestr);
        str->modestr = NULL;
    }

    if (gli_unregister_obj) {
        (*gli_unregister_obj)(str, gidisp_Class_Stream, str->disprock);
        str->disprock.ptr = NULL;
    }

    if (str->tempbufinfo) {
        data_tempbufinfo_free(str->tempbufinfo);
        str->tempbufinfo = NULL;
    }

    prev = str->prev;
    next = str->next;
    str->prev = NULL;
    str->next = NULL;

    if (prev)
        prev->next = next;
    else
        gli_streamlist = next;
    if (next)
        next->prev = prev;

    free(str);
}

int gli_streams_update_from_state(stream_t **list, int count, stream_t *currentstr)
{
    if (gli_streamlist) {
        gli_fatal_error("streams already exist");
        return FALSE;
    }

    int ix;
    for (ix=count-1; ix>=0; ix--) {
        strid_t str = list[ix];
        str->next = gli_streamlist;
        gli_streamlist = str;
        if (str->next) {
            str->next->prev = str;
        }

        if (str->updatetag >= tagcounter)
            tagcounter = str->updatetag + 5;

        if (str->tempbufinfo) {
            data_tempbufinfo_t *info = str->tempbufinfo;
            str->tempbufinfo = NULL;

            void *voidbuf = NULL;
            
            switch (str->type) {

            case strtype_File: {
                FILE *fl = NULL;
                /* The file should already exist, but we'll do the pre-create dance just in case. */
                if (strchr(str->modestr, '+')) {
                    fl = fopen(str->filename, "ab");
                    if (fl)
                        fclose(fl);
                }

                fl = fopen(str->filename, str->modestr);
                if (!fl) {
                    gli_strict_warning("streams_update_from_state: unable to open file.");
                    return FALSE;
                }
                str->file = fl;

                glk_stream_set_position(str, info->bufptr, seekmode_Start);
                /* lastop is now cleared */
            }
            break;
                
            case strtype_Memory: {
                if (!gli_dispatch_restore_arr) {
                    break;
                }
                if (!str->unicode) {
                    str->arrayrock = (*gli_dispatch_restore_arr)(info->bufkey, str->buflen, "&+#!Cn", &voidbuf);
                    if (voidbuf) {
                        str->buf = voidbuf;
                        memset(str->buf, 0, str->buflen);
                        str->bufptr = str->buf + info->bufptr;
                        str->bufeof = str->buf + info->bufeof;
                        str->bufend = str->buf + info->bufend;
                        if (info->bufdata) {
                            if (info->bufdatalen > str->buflen)
                                info->bufdatalen = str->buflen;
                            memcpy(str->buf, info->bufdata, info->bufdatalen);
                        }
                    }
                }
                else {
                    str->arrayrock = (*gli_dispatch_restore_arr)(info->bufkey, str->buflen, "&+#!Iu", &voidbuf);
                    if (voidbuf) {
                        str->ubuf = voidbuf;
                        memset(str->ubuf, 0, sizeof(glui32)*str->buflen);
                        str->ubufptr = str->ubuf + info->bufptr;
                        str->ubufeof = str->ubuf + info->bufeof;
                        str->ubufend = str->ubuf + info->bufend;
                        if (info->ubufdata) {
                            if (info->bufdatalen > str->buflen)
                                info->bufdatalen = str->buflen;
                            memcpy(str->ubuf, info->ubufdata, sizeof(glui32)*info->bufdatalen);
                        }
                    }
                }
                
            }
            break;
            
            } /* end switch */
            
            data_tempbufinfo_free(info);
        }
    }

    gli_currentstr = currentstr;

    return TRUE;
}

void gli_stream_fill_result(stream_t *str, stream_result_t *result)
{
    if (!result)
        return;
    
    result->readcount = str->readcount;
    result->writecount = str->writecount;
}

void glk_stream_close(stream_t *str, stream_result_t *result)
{
    if (!str) {
        gli_strict_warning("stream_close: invalid ref.");
        return;
    }
    
    if (str->type == strtype_Window) {
        gli_strict_warning("stream_close: cannot close window stream");
        return;
    }
    
    gli_stream_fill_result(str, result);
    gli_delete_stream(str);
}

void gli_streams_close_all()
{
    /* This is used only at shutdown time; it closes file streams (the
        only ones that need finalization.) */
    stream_t *str, *strnext;
    
    str=gli_streamlist;
    while (str) {
        strnext = str->next;
        
        if (str->type == strtype_File) {
            gli_delete_stream(str);
        }
        
        str = strnext;
    }
}

strid_t glk_stream_open_memory(char *buf, glui32 buflen, glui32 fmode, 
    glui32 rock)
{
    stream_t *str;
    
    if (fmode != filemode_Read 
        && fmode != filemode_Write 
        && fmode != filemode_ReadWrite) {
        gli_strict_warning("stream_open_memory: illegal filemode");
        return 0;
    }
    
    str = gli_new_stream(strtype_Memory, 
        (fmode != filemode_Write), 
        (fmode != filemode_Read), 
        rock);
    if (!str) {
        gli_strict_warning("stream_open_memory: unable to create stream.");
        return 0;
    }
    
    if (buf && buflen) {
        str->buf = (unsigned char *)buf;
        str->bufptr = (unsigned char *)buf;
        str->buflen = buflen;
        str->bufend = str->buf + str->buflen;
        if (fmode == filemode_Write)
            str->bufeof = (unsigned char *)buf;
        else
            str->bufeof = str->bufend;
        if (gli_register_arr) {
            str->arrayrock = (*gli_register_arr)(buf, buflen, "&+#!Cn");
        }
    }
    
    return str;
}

stream_t *gli_stream_open_window(window_t *win)
{
    stream_t *str;
    
    str = gli_new_stream(strtype_Window, FALSE, TRUE, 0);
    if (!str)
        return NULL;

    str->win = win;
    
    return str;
}

strid_t glk_stream_open_file(fileref_t *fref, glui32 fmode,
    glui32 rock)
{
    stream_t *str;
    char modestr[16];
    FILE *fl;
    
    if (!fref) {
        gli_strict_warning("stream_open_file: invalid fileref ref.");
        return 0;
    }

    /* The spec says that Write, ReadWrite, and WriteAppend create the
       file if necessary. However, fopen(filename, "r+") doesn't create
       a file. So we have to pre-create it in the ReadWrite and
       WriteAppend cases. (We use "a" so as not to truncate, and "b" 
       because we're going to close it immediately, so it doesn't matter.) */

    /* Another Unix quirk: in r+ mode, you're not supposed to flip from
       reading to writing or vice versa without doing an fseek. We will
       track the most recent operation (as lastop) -- Write, Read, or
       0 if either is legal next. */

    if (fmode == filemode_ReadWrite || fmode == filemode_WriteAppend) {
        fl = fopen(fref->filename, "ab");
        if (!fl) {
            gli_strict_warning("stream_open_file: unable to open file.");
            return 0;
        }

        fclose(fl);
    }
    
    switch (fmode) {
        case filemode_Write:
            strcpy(modestr, "w");
            break;
        case filemode_Read:
            strcpy(modestr, "r");
            break;
        case filemode_ReadWrite:
            strcpy(modestr, "r+");
            break;
        case filemode_WriteAppend:
            /* Can't use "a" here, because then fseek wouldn't work.
               Instead we use "r+" and then fseek to the end. */
            strcpy(modestr, "r+");
            break;
    }
    
    if (!fref->textmode)
        strcat(modestr, "b");
        
    fl = fopen(fref->filename, modestr);
    if (!fl) {
        /* According to recent spec discussion, we only display an error on missing files for write/append mode. In read mode, we return silently. */
        if (fmode != filemode_Read) {
            gli_strict_warning("stream_open_file: unable to open file.");
        }
        return 0;
    }
    
    if (fmode == filemode_WriteAppend) {
        fseek(fl, 0, SEEK_END); /* ...to the end. */
    }

    str = gli_new_stream(strtype_File, 
        (fmode == filemode_Read || fmode == filemode_ReadWrite), 
        !(fmode == filemode_Read), 
        rock);
    if (!str) {
        gli_strict_warning("stream_open_file: unable to create stream.");
        fclose(fl);
        return 0;
    }
    
    str->isbinary = !fref->textmode;
    str->file = fl;
    str->lastop = 0;
    
    /* This is only needed for the autosave record. */
    str->filename = strdup(fref->filename);
    str->modestr = strdup(modestr);
    
    return str;
}

#ifdef GLK_MODULE_UNICODE

strid_t glk_stream_open_memory_uni(glui32 *ubuf, glui32 buflen, glui32 fmode, 
    glui32 rock)
{
    stream_t *str;

    if (fmode != filemode_Read 
        && fmode != filemode_Write 
        && fmode != filemode_ReadWrite) {
        gli_strict_warning("stream_open_memory_uni: illegal filemode");
        return NULL;
    }
    
    str = gli_new_stream(strtype_Memory, 
        (fmode != filemode_Write), 
        (fmode != filemode_Read), 
        rock);
    if (!str) {
        gli_strict_warning("stream_open_memory_uni: unable to create stream.");
        return NULL;
    }
    
    str->unicode = TRUE;

    if (ubuf && buflen) {
        str->ubuf = ubuf;
        str->ubufptr = ubuf;
        str->buflen = buflen;
        str->ubufend = str->ubuf + str->buflen;
        if (fmode == filemode_Write)
            str->ubufeof = ubuf;
        else
            str->ubufeof = str->ubufend;
        if (gli_register_arr) {
            str->arrayrock = (*gli_register_arr)(ubuf, buflen, "&+#!Iu");
        }
    }
    
    return str;
}

strid_t glk_stream_open_file_uni(fileref_t *fref, glui32 fmode,
    glui32 rock)
{
    strid_t str = glk_stream_open_file(fref, fmode, rock);
    if (!str)
        return NULL;
    /* Unlovely, but it works in this library */
    str->unicode = TRUE;
    return str;
}

#endif /* GLK_MODULE_UNICODE */


#ifdef GLK_MODULE_RESOURCE_STREAM

strid_t glk_stream_open_resource(glui32 filenum, glui32 rock)
{
    strid_t str;
    int isbinary;
    giblorb_err_t err;
    giblorb_result_t res;

    if (gli_get_dataresource_info(filenum, &res.data.ptr, &res.length, &isbinary)) {
        /* Success! */
    }
    else {
        /* No command-line pathname; check blorb. */
        
        giblorb_map_t *map = giblorb_get_resource_map();
        if (!map)
            return 0; /* Not running from a blorb file */

        err = giblorb_load_resource(map, giblorb_method_Memory, &res, giblorb_ID_Data, filenum);
        if (err)
            return 0; /* Not found, or some other error */

        /* Note that binary chunks are normally type BINA, but FORM
           chunks also count as binary. (This allows us to embed AIFF
           files as readable resources, for example.) */

        if (res.chunktype == giblorb_ID_TEXT)
            isbinary = FALSE;
        else if (res.chunktype == giblorb_ID_BINA
            || res.chunktype == giblorb_make_id('F', 'O', 'R', 'M'))
            isbinary = TRUE;
        else
            return 0; /* Unknown chunk type */
    }

    /* We'll use the in-memory copy of the chunk data as the basis for
       our new stream. It's important to not call chunk_unload() until
       the stream is closed (and we won't). 
       
       This will be memory-hoggish for giant data chunks, but I don't
       expect giant data chunks at this point. A more efficient model
       would be to use the file on disk, but this requires some hacking
       into the file stream code (we'd need to open a new FILE*) and
       I don't feel like doing that.
    */

    str = gli_new_stream(strtype_Resource,
        TRUE, FALSE, rock);
    if (!str) {
        gli_strict_warning("stream_open_resource: unable to create stream.");
        return NULL;
    }

    str->isbinary = isbinary;
    str->fileresnum = filenum; /* Only needed for the autosave record */
    
    if (res.data.ptr && res.length) {
        str->buf = (unsigned char *)res.data.ptr;
        str->bufptr = (unsigned char *)res.data.ptr;
        str->buflen = res.length;
        str->bufend = str->buf + str->buflen;
        str->bufeof = str->bufend;
    }
    
    return str;
}

strid_t glk_stream_open_resource_uni(glui32 filenum, glui32 rock)
{
    strid_t str;
    int isbinary;
    giblorb_err_t err;
    giblorb_result_t res;

    if (gli_get_dataresource_info(filenum, &res.data.ptr, &res.length, &isbinary)) {
        /* Success! */
    }
    else {
        /* No explicit path; check blorb. */
        
        giblorb_map_t *map = giblorb_get_resource_map();
        if (!map)
            return 0; /* Not running from a blorb file */

        err = giblorb_load_resource(map, giblorb_method_Memory, &res, giblorb_ID_Data, filenum);
        if (err)
            return 0; /* Not found, or some other error */

        if (res.chunktype == giblorb_ID_TEXT)
            isbinary = FALSE;
        else if (res.chunktype == giblorb_ID_BINA
            || res.chunktype == giblorb_make_id('F', 'O', 'R', 'M'))
            isbinary = TRUE;
        else
            return 0; /* Unknown chunk type */
    }

    str = gli_new_stream(strtype_Resource, 
        TRUE, FALSE, rock);
    if (!str) {
        gli_strict_warning("stream_open_resource_uni: unable to create stream.");
        return NULL;
    }
    
    str->unicode = TRUE;
    str->isbinary = isbinary;

    /* We have been handed an array of bytes. (They're big-endian
       four-byte chunks, or perhaps a UTF-8 byte sequence, rather than
       native-endian four-byte integers). So we drop it into buf,
       rather than ubuf -- we'll have to do the translation in the
       get() functions. */

    if (res.data.ptr && res.length) {
        str->buf = (unsigned char *)res.data.ptr;
        str->bufptr = (unsigned char *)res.data.ptr;
        str->buflen = res.length;
        str->bufend = str->buf + str->buflen;
        str->bufeof = str->bufend;
    }
    
    return str;
}

#endif /* GLK_MODULE_RESOURCE_STREAM */

strid_t gli_stream_open_pathname(char *pathname, int writemode, 
    int textmode, glui32 rock)
{
    char modestr[16];
    stream_t *str;
    FILE *fl;
    
    if (!writemode)
        strcpy(modestr, "r");
    else
        strcpy(modestr, "w");
    if (!textmode)
        strcat(modestr, "b");
        
    fl = fopen(pathname, modestr);
    if (!fl) {
        return 0;
    }

    str = gli_new_stream(strtype_File, 
        !writemode, writemode, rock);
    if (!str) {
        fclose(fl);
        return 0;
    }
    
    str->file = fl;
    str->lastop = 0;
    str->filename = strdup(pathname);
    str->modestr = strdup(modestr);
    
    return str;
}

strid_t glk_stream_iterate(strid_t str, glui32 *rock)
{
    if (!str) {
        str = gli_streamlist;
    }
    else {
        str = str->next;
    }
    
    if (str) {
        if (rock)
            *rock = str->rock;
        return str;
    }
    
    if (rock)
        *rock = 0;
    return NULL;
}

glui32 glk_stream_get_rock(stream_t *str)
{
    if (!str) {
        gli_strict_warning("stream_get_rock: invalid ref.");
        return 0;
    }
    
    return str->rock;
}

void gli_stream_set_current(stream_t *str)
{
    gli_currentstr = str;
}

void glk_stream_set_current(stream_t *str)
{
    gli_stream_set_current(str);
}

strid_t glk_stream_get_current()
{
    if (gli_currentstr)
        return gli_currentstr;
    else
        return 0;
}

void glk_stream_set_position(stream_t *str, glsi32 pos, glui32 seekmode)
{
    if (!str) {
        gli_strict_warning("stream_set_position: invalid ref");
        return;
    }

    switch (str->type) {
        case strtype_Memory: 
        case strtype_Resource: 
            if (!str->unicode || str->type == strtype_Resource) {
                if (seekmode == seekmode_Current) {
                    pos = (str->bufptr - str->buf) + pos;
                }
                else if (seekmode == seekmode_End) {
                    pos = (str->bufeof - str->buf) + pos;
                }
                else {
                    /* pos = pos */
                }
                if (pos < 0)
                    pos = 0;
                if (pos > (str->bufeof - str->buf))
                    pos = (str->bufeof - str->buf);
                str->bufptr = str->buf + pos;
            }
            else {
                if (seekmode == seekmode_Current) {
                    pos = (str->ubufptr - str->ubuf) + pos;
                }
                else if (seekmode == seekmode_End) {
                    pos = (str->ubufeof - str->ubuf) + pos;
                }
                else {
                    /* pos = pos */
                }
                if (pos < 0)
                    pos = 0;
                if (pos > (str->ubufeof - str->ubuf))
                    pos = (str->ubufeof - str->ubuf);
                str->ubufptr = str->ubuf + pos;
            }
            break;
        case strtype_Window:
            /* do nothing; don't pass to echo stream */
            break;
        case strtype_File:
            /* Either reading or writing is legal after an fseek. */
            str->lastop = 0;
            if (str->unicode) {
                /* Use 4 here, rather than sizeof(glui32). */
                pos *= 4;
            }
            fseek(str->file, pos, 
                ((seekmode == seekmode_Current) ? 1 :
                ((seekmode == seekmode_End) ? 2 : 0)));
            break;
    }   
}

glui32 glk_stream_get_position(stream_t *str)
{
    if (!str) {
        gli_strict_warning("stream_get_position: invalid ref");
        return 0;
    }

    switch (str->type) {
        case strtype_Memory: 
        case strtype_Resource: 
            if (!str->unicode || str->type == strtype_Resource) {
                return (str->bufptr - str->buf);
            }
            else {
                return (str->ubufptr - str->ubuf);
            }
        case strtype_File:
            if (!str->unicode) {
                return ftell(str->file);
            }
            else {
                /* Use 4 here, rather than sizeof(glui32). */
                return ftell(str->file) / 4;
            }
        case strtype_Window:
        default:
            return 0;
    }   
}

static void gli_stream_ensure_op(stream_t *str, glui32 op)
{
    /* We have to do an fseek() between reading and writing. This will
       only come up for ReadWrite or WriteAppend files. */
    if (str->lastop != 0 && str->lastop != op) {
        long pos = ftell(str->file);
        fseek(str->file, pos, SEEK_SET);
    }
    str->lastop = op;
}

static void gli_put_char(stream_t *str, unsigned char ch)
{
    if (!str || !str->writable)
        return;

    str->writecount++;
    
    switch (str->type) {
        case strtype_Memory:
            if (!str->unicode) {
                if (str->bufptr < str->bufend) {
                    *(str->bufptr) = ch;
                    str->bufptr++;
                    if (str->bufptr > str->bufeof)
                        str->bufeof = str->bufptr;
                }
            }
            else {
                if (str->ubufptr < str->ubufend) {
                    *(str->ubufptr) = (glui32)ch;
                    str->ubufptr++;
                    if (str->ubufptr > str->ubufeof)
                        str->ubufeof = str->ubufptr;
                }
            }
            break;
        case strtype_Window:
            if (str->win->line_request) {
                gli_strict_warning("put_char: window has pending line request");
                break;
            }
            gli_window_put_char(str->win, ch);
            if (str->win->echostr)
                gli_put_char(str->win->echostr, ch);
            break;
        case strtype_File:
            gli_stream_ensure_op(str, filemode_Write);
            if (!str->unicode) {
                putc(ch, str->file);
            }
            else {
                if (!str->isbinary) {
                    /* cheap UTF-8 stream */
                    gli_putchar_utf8(ch, str->file);
                }
                else {
                    /* cheap big-endian stream */
                    putc(0, str->file);
                    putc(0, str->file);
                    putc(0, str->file);
                    putc(ch, str->file);
                }
            }
            break;
        case strtype_Resource:
            /* resource streams are never writable */
            break;
    }
}

#ifdef GLK_MODULE_UNICODE

static void gli_put_char_uni(stream_t *str, glui32 ch)
{
    if (!str || !str->writable)
        return;

    str->writecount++;
    
    switch (str->type) {
        case strtype_Memory:
            if (!str->unicode) {
                if (ch >= 0x100)
                    ch = '?';
                if (str->bufptr < str->bufend) {
                    *(str->bufptr) = ch;
                    str->bufptr++;
                    if (str->bufptr > str->bufeof)
                        str->bufeof = str->bufptr;
                }
            }
            else {
                if (str->ubufptr < str->ubufend) {
                    *(str->ubufptr) = ch;
                    str->ubufptr++;
                    if (str->ubufptr > str->ubufeof)
                        str->ubufeof = str->ubufptr;
                }
            }
            break;
        case strtype_Window:
            if (str->win->line_request) {
                gli_strict_warning("put_char_uni: window has pending line request");
                break;
            }
            gli_window_put_char(str->win, ch);
            if (str->win->echostr)
                gli_put_char_uni(str->win->echostr, ch);
            break;
        case strtype_File:
            gli_stream_ensure_op(str, filemode_Write);
            if (!str->unicode) {
                if (ch >= 0x100)
                    ch = '?';
                putc(ch, str->file);
            }
            else {
                if (!str->isbinary) {
                    /* cheap UTF-8 stream */
                    gli_putchar_utf8(ch, str->file);
                }
                else {
                    /* cheap big-endian stream */
                    putc(((ch >> 24) & 0xFF), str->file);
                    putc(((ch >> 16) & 0xFF), str->file);
                    putc(((ch >>  8) & 0xFF), str->file);
                    putc( (ch        & 0xFF), str->file);
                }
            }
            break;
        case strtype_Resource:
            /* resource streams are never writable */
            break;
    }
}

#endif /* GLK_MODULE_UNICODE */

static void gli_put_buffer(stream_t *str, char *buf, glui32 len)
{
    unsigned char *cx;
    glui32 lx;
    
    if (!str || !str->writable)
        return;

    str->writecount += len;
    
    switch (str->type) {
        case strtype_Memory:
            if (!str->unicode) {
                if (str->bufptr >= str->bufend) {
                    len = 0;
                }
                else {
                    if (str->bufptr + len > str->bufend) {
                        lx = (str->bufptr + len) - str->bufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                if (len) {
                    memcpy(str->bufptr, buf, len);
                    str->bufptr += len;
                    if (str->bufptr > str->bufeof)
                        str->bufeof = str->bufptr;
                }
            }
            else {
                if (str->ubufptr >= str->ubufend) {
                    len = 0;
                }
                else {
                    if (str->ubufptr + len > str->ubufend) {
                        lx = (str->ubufptr + len) - str->ubufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                if (len) {
                    for (lx=0; lx<len; lx++) {
                        *str->ubufptr = (unsigned char)(buf[lx]);
                        str->ubufptr++;
                    }
                    if (str->ubufptr > str->ubufeof)
                        str->ubufeof = str->ubufptr;
                }
            }
            break;
        case strtype_Window:
            if (str->win->line_request) {
                gli_strict_warning("put_buffer: window has pending line request");
                break;
            }
            for (lx=0, cx=(unsigned char *)buf; lx<len; lx++, cx++) {
                gli_window_put_char(str->win, *cx);
            }
            if (str->win->echostr)
                gli_put_buffer(str->win->echostr, buf, len);
            break;
        case strtype_File:
            gli_stream_ensure_op(str, filemode_Write);
            if (!str->unicode) {
                fwrite((unsigned char *)buf, 1, len, str->file);
            }
            else {
                if (!str->isbinary) {
                    /* cheap UTF-8 stream */
                    for (lx=0; lx<len; lx++)
                        gli_putchar_utf8(((unsigned char *)buf)[lx], str->file);
                }
                else {
                    /* cheap big-endian stream */
                    for (lx=0; lx<len; lx++) {
                        unsigned char ch = ((unsigned char *)buf)[lx];
                        putc(((ch >> 24) & 0xFF), str->file);
                        putc(((ch >> 16) & 0xFF), str->file);
                        putc(((ch >>  8) & 0xFF), str->file);
                        putc( (ch        & 0xFF), str->file);
                    }
                }
            }
            break;
        case strtype_Resource:
            /* resource streams are never writable */
            break;
    }
}

static void gli_set_style(stream_t *str, glui32 val)
{
    if (!str || !str->writable)
        return;

    if (val >= style_NUMSTYLES)
        val = 0;
    
    switch (str->type) {
        case strtype_Window:
            str->win->style = val;
            if (str->win->echostr)
                gli_set_style(str->win->echostr, val);
            break;
    }
}

static void gli_set_hyperlink(stream_t *str, glui32 linkval)
{
    if (!str || !str->writable)
        return;

    if (!gli_supportcaps.hyperlinks)
        return;
    
    switch (str->type) {
        case strtype_Window:
            str->win->hyperlink = linkval;
            if (str->win->echostr)
                gli_set_hyperlink(str->win->echostr, linkval);
            break;
    }
}

void gli_stream_echo_line(stream_t *str, char *buf, glui32 len)
{
    /* This is only used to echo line input to an echo stream. See
        the line input methods in gtw_grid and gtw_buf. */
    gli_put_buffer(str, buf, len);
    gli_put_char(str, '\n');
}

#ifdef GLK_MODULE_UNICODE

void gli_stream_echo_line_uni(stream_t *str, glui32 *buf, glui32 len)
{
    glui32 ix;
    /* This is only used to echo line input to an echo stream. See
        glk_select(). */
    for (ix=0; ix<len; ix++) {
        gli_put_char_uni(str, buf[ix]);
    }
    gli_put_char(str, '\n');
}

#else

void gli_stream_echo_line_uni(stream_t *str, glui32 *buf, glui32 len)
{
    gli_strict_warning("stream_echo_line_uni: called with no Unicode line request");
}

#endif /* GLK_MODULE_UNICODE */

static glsi32 gli_get_char(stream_t *str, int want_unicode)
{
    if (!str || !str->readable)
        return -1;
    
    switch (str->type) {
        case strtype_Resource:
            if (str->unicode) {
                glui32 ch = 0;
                if (str->isbinary) {
                    /* cheap big-endian stream */
                    if (str->bufptr >= str->bufend)
                        return -1;
                    ch = *(str->bufptr);
                    str->bufptr++;
                    if (str->bufptr >= str->bufend)
                        return -1;
                    ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                    str->bufptr++;
                    if (str->bufptr >= str->bufend)
                        return -1;
                    ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                    str->bufptr++;
                    if (str->bufptr >= str->bufend)
                        return -1;
                    ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                    str->bufptr++;
                }
                else {
                    /* slightly less cheap UTF8 stream */
                    glui32 val0, val1, val2, val3;
                    int flag = UTF8_DECODE_INLINE(&ch, (str->bufptr >= str->bufend), (*(str->bufptr++)), val0, val1, val2, val3);
                    if (!flag)
                        return -1;
                }
                str->readcount++;
                if (!want_unicode && ch >= 0x100)
                    return '?';
                return (glsi32)ch;
            }
            /* for text streams, fall through to memory case */
        case strtype_Memory:
            if (!str->unicode) {
                if (str->bufptr < str->bufend) {
                    unsigned char ch;
                    ch = *(str->bufptr);
                    str->bufptr++;
                    str->readcount++;
                    return ch;
                }
                else {
                    return -1;
                }
            }
            else {
                if (str->ubufptr < str->ubufend) {
                    glui32 ch;
                    ch = *(str->ubufptr);
                    str->ubufptr++;
                    str->readcount++;
                    if (!want_unicode && ch >= 0x100)
                        return '?';
                    return ch;
                }
                else {
                    return -1;
                }
            }
        case strtype_File: 
            gli_stream_ensure_op(str, filemode_Read);
            if (!str->unicode) {
                int res;
                res = getc(str->file);
                if (res != -1) {
                    str->readcount++;
                    return (glsi32)res;
                }
                else {
                    return -1;
                }
            }
            else if (str->isbinary) {
                /* cheap big-endian stream */
                int res;
                glui32 ch = 0;
                res = getc(str->file);
                if (res == -1)
                    return -1;
                ch = (res & 0xFF);
                res = getc(str->file);
                if (res == -1)
                    return -1;
                ch = (ch << 8) | (res & 0xFF);
                res = getc(str->file);
                if (res == -1)
                    return -1;
                ch = (ch << 8) | (res & 0xFF);
                res = getc(str->file);
                if (res == -1)
                    return -1;
                ch = (ch << 8) | (res & 0xFF);
                str->readcount++;
                if (!want_unicode && ch >= 0x100)
                    return '?';
                return (glsi32)ch;
            }
            else {
                /* slightly less cheap UTF-8 stream */
                glui32 val0, val1, val2, val3;
                int res;
                glui32 ch = 0;
                int flag = UTF8_DECODE_INLINE(&ch, (res=getc(str->file), res == -1), (res & 0xFF), val0, val1, val2, val3);
                if (!flag)
                    return -1;
                str->readcount++;
                if (!want_unicode && ch >= 0x100)
                    return '?';
                return (glsi32)ch;
            }
        case strtype_Window:
        default:
            return -1;
    }
}

static glui32 gli_get_buffer(stream_t *str, char *cbuf, glui32 *ubuf,
    glui32 len)
{
    if (!str || !str->readable)
        return 0;
    
    switch (str->type) {
        case strtype_Resource:
            if (str->unicode) {
                glui32 count = 0;
                while (count < len) {
                    glui32 ch = 0;
                    if (str->isbinary) {
                        /* cheap big-endian stream */
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = *(str->bufptr);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                    }
                    else {
                        /* slightly less cheap UTF8 stream */
                        glui32 val0, val1, val2, val3;
                        int flag = UTF8_DECODE_INLINE(&ch, (str->bufptr >= str->bufend), (*(str->bufptr++)), val0, val1, val2, val3);
                        if (!flag)
                            break;
                    }
                    if (cbuf) {
                        if (ch >= 0x100)
                            cbuf[count] = '?';
                        else
                            cbuf[count] = ch;
                    }
                    else {
                        ubuf[count] = ch;
                    }
                    count++;
                }
                str->readcount += count;
                return count;
            }
            /* for text streams, fall through to memory case */
        case strtype_Memory:
            if (!str->unicode) {
                if (str->bufptr >= str->bufend) {
                    len = 0;
                }
                else {
                    if (str->bufptr + len > str->bufend) {
                        glui32 lx;
                        lx = (str->bufptr + len) - str->bufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                if (len) {
                    if (cbuf) {
                        memcpy(cbuf, str->bufptr, len);
                    }
                    else {
                        glui32 lx;
                        for (lx=0; lx<len; lx++) {
                            ubuf[lx] = (unsigned char)str->bufptr[lx];
                        }
                    }
                    str->bufptr += len;
                    if (str->bufptr > str->bufeof)
                        str->bufeof = str->bufptr;
                }
            }
            else {
                if (str->ubufptr >= str->ubufend) {
                    len = 0;
                }
                else {
                    if (str->ubufptr + len > str->ubufend) {
                        glui32 lx;
                        lx = (str->ubufptr + len) - str->ubufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                if (len) {
                    glui32 lx, ch;
                    if (cbuf) {
                        for (lx=0; lx<len; lx++) {
                            ch = str->ubufptr[lx];
                            if (ch >= 0x100)
                                ch = '?';
                            cbuf[lx] = ch;
                        }
                    }
                    else {
                        for (lx=0; lx<len; lx++) {
                            ubuf[lx] = str->ubufptr[lx];
                        }
                    }
                    str->ubufptr += len;
                    if (str->ubufptr > str->ubufeof)
                        str->ubufeof = str->ubufptr;
                }
            }
            str->readcount += len;
            return len;
        case strtype_File: 
            gli_stream_ensure_op(str, filemode_Read);
            if (!str->unicode) {
                if (cbuf) {
                    glui32 res;
                    res = fread(cbuf, 1, len, str->file);
                    str->readcount += res;
                    return res;
                }
                else {
                    glui32 lx;
                    for (lx=0; lx<len; lx++) {
                        int res;
                        glui32 ch;
                        res = getc(str->file);
                        if (res == -1)
                            break;
                        ch = (res & 0xFF);
                        str->readcount++;
                        ubuf[lx] = ch;
                    }
                    return lx;
                }
            }
            else if (str->isbinary) {
                /* cheap big-endian stream */
                glui32 lx;
                for (lx=0; lx<len; lx++) {
                    int res;
                    glui32 ch;
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    str->readcount++;
                    if (cbuf) {
                        if (ch >= 0x100)
                            ch = '?';
                        cbuf[lx] = ch;
                    }
                    else {
                        ubuf[lx] = ch;
                    }
                }
                return lx;
            }
            else {
                /* slightly less cheap UTF-8 stream */
                glui32 lx;
                for (lx=0; lx<len; lx++) {
                    glui32 val0, val1, val2, val3;
                    int res;
                    glui32 ch = 0;
                    int flag = UTF8_DECODE_INLINE(&ch, (res=getc(str->file), res == -1), (res & 0xFF), val0, val1, val2, val3);
                    if (!flag)
                        break;
                    str->readcount++;
                    if (cbuf) {
                        if (ch >= 0x100)
                            ch = '?';
                        cbuf[lx] = ch;
                    }
                    else {
                        ubuf[lx] = ch;
                    }
                }
                return lx;
            }
        case strtype_Window:
        default:
            return 0;
    }
}

static glui32 gli_get_line(stream_t *str, char *cbuf, glui32 *ubuf, 
    glui32 len)
{
    glui32 lx;
    int gotnewline;

    if (!str || !str->readable)
        return 0;
    
    switch (str->type) {
        case strtype_Resource:
            if (len == 0)
                return 0;
            len -= 1; /* for the terminal null */
            if (str->unicode) {
                glui32 count = 0;
                while (count < len) {
                    glui32 ch = 0;
                    if (str->isbinary) {
                        /* cheap big-endian stream */
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = *(str->bufptr);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                        if (str->bufptr >= str->bufend)
                            break;
                        ch = (ch << 8) | (*(str->bufptr) & 0xFF);
                        str->bufptr++;
                    }
                    else {
                        /* slightly less cheap UTF8 stream */
                        glui32 val0, val1, val2, val3;
                        int flag = UTF8_DECODE_INLINE(&ch, (str->bufptr >= str->bufend), (*(str->bufptr++)), val0, val1, val2, val3);
                        if (!flag)
                            break;
                    }
                    if (cbuf) {
                        if (ch >= 0x100)
                            cbuf[count] = '?';
                        else
                            cbuf[count] = ch;
                    }
                    else {
                        ubuf[count] = ch;
                    }
                    count++;
                    if (ch == '\n')
                        break;
                }
                if (cbuf)
                    cbuf[count] = '\0';
                else
                    ubuf[count] = '\0';
                str->readcount += count;
                return count;
            }
            /* for text streams, fall through to memory case */
        case strtype_Memory:
            if (len == 0)
                return 0;
            len -= 1; /* for the terminal null */
            if (!str->unicode) {
                if (str->bufptr >= str->bufend) {
                    len = 0;
                }
                else {
                    if (str->bufptr + len > str->bufend) {
                        lx = (str->bufptr + len) - str->bufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                gotnewline = FALSE;
                if (cbuf) {
                    for (lx=0; lx<len && !gotnewline; lx++) {
                        cbuf[lx] = str->bufptr[lx];
                        gotnewline = (cbuf[lx] == '\n');
                    }
                    cbuf[lx] = '\0';
                }
                else {
                    for (lx=0; lx<len && !gotnewline; lx++) {
                        ubuf[lx] = (unsigned char)str->bufptr[lx];
                        gotnewline = (ubuf[lx] == '\n');
                    }
                    ubuf[lx] = '\0';
                }
                str->bufptr += lx;
            }
            else {
                if (str->ubufptr >= str->ubufend) {
                    len = 0;
                }
                else {
                    if (str->ubufptr + len > str->ubufend) {
                        lx = (str->ubufptr + len) - str->ubufend;
                        if (lx < len)
                            len -= lx;
                        else
                            len = 0;
                    }
                }
                gotnewline = FALSE;
                if (cbuf) {
                    for (lx=0; lx<len && !gotnewline; lx++) {
                        glui32 ch;
                        ch = str->ubufptr[lx];
                        if (ch >= 0x100)
                            ch = '?';
                        cbuf[lx] = ch;
                        gotnewline = (ch == '\n');
                    }
                    cbuf[lx] = '\0';
                }
                else {
                    for (lx=0; lx<len && !gotnewline; lx++) {
                        glui32 ch;
                        ch = str->ubufptr[lx];
                        ubuf[lx] = ch;
                        gotnewline = (ch == '\n');
                    }
                    ubuf[lx] = '\0';
                }
                str->ubufptr += lx;
            }
            str->readcount += lx;
            return lx;
        case strtype_File: 
            gli_stream_ensure_op(str, filemode_Read);
            if (!str->unicode) {
                if (cbuf) {
                    char *res;
                    res = fgets(cbuf, len, str->file);
                    if (!res) {
                        return 0;
                    }
                    else {
                        glui32 lx = strlen(cbuf);
                        str->readcount += lx;
                        return lx;
                    }
                }
                else {
                    glui32 lx;
                    if (len == 0)
                        return 0;
                    len -= 1; /* for the terminal null */
                    gotnewline = FALSE;
                    for (lx=0; lx<len && !gotnewline; lx++) {
                        int res;
                        glui32 ch;
                        res = getc(str->file);
                        if (res == -1)
                            break;
                        ch = (res & 0xFF);
                        str->readcount++;
                        ubuf[lx] = ch;
                        gotnewline = (ch == '\n');
                    }
                    ubuf[lx] = '\0';
                    return lx;
                }
            }
            else if (str->isbinary) {
                /* cheap big-endian stream */
                glui32 lx;
                if (len == 0)
                    return 0;
                len -= 1; /* for the terminal null */
                gotnewline = FALSE;
                for (lx=0; lx<len && !gotnewline; lx++) {
                    int res;
                    glui32 ch;
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    res = getc(str->file);
                    if (res == -1)
                        break;
                    ch = (ch << 8) | (res & 0xFF);
                    str->readcount++;
                    if (cbuf) {
                        if (ch >= 0x100)
                            ch = '?';
                        cbuf[lx] = ch;
                    }
                    else {
                        ubuf[lx] = ch;
                    }
                    gotnewline = (ch == '\n');
                }
                if (cbuf)
                    cbuf[lx] = '\0';
                else 
                    ubuf[lx] = '\0';
                return lx;
            }
            else {
                /* slightly less cheap UTF-8 stream */
                glui32 lx;
                if (len == 0)
                    return 0;
                len -= 1; /* for the terminal null */
                gotnewline = FALSE;
                for (lx=0; lx<len && !gotnewline; lx++) {
                    glui32 val0, val1, val2, val3;
                    int res;
                    glui32 ch = 0;
                    int flag = UTF8_DECODE_INLINE(&ch, (res=getc(str->file), res == -1), (res & 0xFF), val0, val1, val2, val3);
                    if (!flag)
                        break;
                    str->readcount++;
                    if (cbuf) {
                        if (ch >= 0x100)
                            ch = '?';
                        cbuf[lx] = ch;
                    }
                    else {
                        ubuf[lx] = ch;
                    }
                    gotnewline = (ch == '\n');
                }
                if (cbuf)
                    cbuf[lx] = '\0';
                else 
                    ubuf[lx] = '\0';
                return lx;
            }
        case strtype_Window:
        default:
            return 0;
    }
}

void glk_put_char(unsigned char ch)
{
    gli_put_char(gli_currentstr, ch);
}

void glk_put_char_stream(stream_t *str, unsigned char ch)
{
    if (!str) {
        gli_strict_warning("put_char_stream: invalid ref");
        return;
    }
    gli_put_char(str, ch);
}

void glk_put_string(char *s)
{
    gli_put_buffer(gli_currentstr, s, strlen(s));
}

void glk_put_string_stream(stream_t *str, char *s)
{
    if (!str) {
        gli_strict_warning("put_string_stream: invalid ref");
        return;
    }
    gli_put_buffer(str, s, strlen(s));
}

void glk_put_buffer(char *buf, glui32 len)
{
    gli_put_buffer(gli_currentstr, buf, len);
}

void glk_put_buffer_stream(stream_t *str, char *buf, glui32 len)
{
    if (!str) {
        gli_strict_warning("put_string_stream: invalid ref");
        return;
    }
    gli_put_buffer(str, buf, len);
}

#ifdef GLK_MODULE_UNICODE

void glk_put_char_uni(glui32 ch)
{
    gli_put_char_uni(gli_currentstr, ch);
}

void glk_put_char_stream_uni(stream_t *str, glui32 ch)
{
    if (!str) {
        gli_strict_warning("put_char_stream: invalid ref");
        return;
    }
    gli_put_char_uni(str, ch);
}

void glk_put_string_uni(glui32 *us)
{
    int len = 0;
    glui32 val;

    while (1) {
        val = us[len];
        if (!val)
            break;
        gli_put_char_uni(gli_currentstr, val);
        len++;
    }
}

void glk_put_string_stream_uni(stream_t *str, glui32 *us)
{
    int len = 0;
    glui32 val;

    if (!str) {
        gli_strict_warning("put_string_stream: invalid ref");
        return;
    }

    while (1) {
        val = us[len];
        if (!val)
            break;
        gli_put_char_uni(str, val);
        len++;
    }
}

void glk_put_buffer_uni(glui32 *buf, glui32 len)
{
    glui32 ix;
    for (ix=0; ix<len; ix++) {
        gli_put_char_uni(gli_currentstr, buf[ix]);
    }
}

void glk_put_buffer_stream_uni(stream_t *str, glui32 *buf, glui32 len)
{
    glui32 ix;
    if (!str) {
        gli_strict_warning("put_string_stream: invalid ref");
        return;
    }
    for (ix=0; ix<len; ix++) {
        gli_put_char_uni(str, buf[ix]);
    }
}

glsi32 glk_get_char_stream_uni(strid_t str)
{
    if (!str) {
        gli_strict_warning("get_char_stream_uni: invalid ref");
        return -1;
    }
    return gli_get_char(str, 1);
}

glui32 glk_get_buffer_stream_uni(strid_t str, glui32 *buf, glui32 len)
{
    if (!str) {
        gli_strict_warning("get_buffer_stream_uni: invalid ref");
        return -1;
    }
    return gli_get_buffer(str, NULL, buf, len);
}

glui32 glk_get_line_stream_uni(strid_t str, glui32 *buf, glui32 len)
{
    if (!str) {
        gli_strict_warning("get_line_stream_uni: invalid ref");
        return -1;
    }
    return gli_get_line(str, NULL, buf, len);
}

#endif /* GLK_MODULE_UNICODE */

void glk_set_style(glui32 val)
{
    gli_set_style(gli_currentstr, val);
}

void glk_set_style_stream(stream_t *str, glui32 val)
{
    if (!str) {
        gli_strict_warning("set_style_stream: invalid ref");
        return;
    }
    gli_set_style(str, val);
}

glsi32 glk_get_char_stream(stream_t *str)
{
    if (!str) {
        gli_strict_warning("get_char_stream: invalid ref");
        return -1;
    }
    return gli_get_char(str, 0);
}

glui32 glk_get_line_stream(stream_t *str, char *buf, glui32 len)
{
    if (!str) {
        gli_strict_warning("get_line_stream: invalid ref");
        return -1;
    }
    return gli_get_line(str, buf, NULL, len);
}

glui32 glk_get_buffer_stream(stream_t *str, char *buf, glui32 len)
{
    if (!str) {
        gli_strict_warning("get_buffer_stream: invalid ref");
        return -1;
    }
    return gli_get_buffer(str, buf, NULL, len);
}

#ifdef GLK_MODULE_HYPERLINKS

void glk_set_hyperlink(glui32 linkval)
{
    gli_set_hyperlink(gli_currentstr, linkval);
}

void glk_set_hyperlink_stream(strid_t str, glui32 linkval)
{
    if (!str) {
        gli_strict_warning("set_hyperlink_stream: invalid ref");
        return;
    }
    gli_set_hyperlink(str, linkval);
}

#endif /* GLK_MODULE_HYPERLINKS */
