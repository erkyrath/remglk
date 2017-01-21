/* rgdata.c: JSON data structure objects
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "glk.h"
#include "remglk.h"
#include "rgdata.h"

/* RawType encodes the type of a JSON data element. */
typedef enum RawType_enum {
    rawtyp_None = 0,
    rawtyp_Number = 1,
    rawtyp_Str = 2,
    rawtyp_List = 3,
    rawtyp_Struct = 4,
    rawtyp_True = 5,
    rawtyp_False = 6,
    rawtyp_Null = 7,
} RawType;

typedef struct strint_struct {
    char *str;
    glui32 val;
} strint_t;

static strint_t special_char_table[] = {
    { "left", keycode_Left },
    { "right", keycode_Right },
    { "up", keycode_Up },
    { "down", keycode_Down },
    { "return", keycode_Return },
    { "delete", keycode_Delete },
    { "escape", keycode_Escape },
    { "tab", keycode_Tab },
    { "pageup", keycode_PageUp },
    { "pagedown", keycode_PageDown },
    { "home", keycode_Home },
    { "end", keycode_End },
    { "func1", keycode_Func1 },
    { "func2", keycode_Func2 },
    { "func3", keycode_Func3 },
    { "func4", keycode_Func4 },
    { "func5", keycode_Func5 },
    { "func6", keycode_Func6 },
    { "func7", keycode_Func7 },
    { "func8", keycode_Func8 },
    { "func9", keycode_Func9 },
    { "func10", keycode_Func10 },
    { "func11", keycode_Func11 },
    { "func12", keycode_Func12 },
    { NULL, 0 }
};

typedef struct data_raw_struct data_raw_t;

/* data_raw_t: Encodes a JSON data object. For lists and structs,
   this contains further JSON structures recursively. All text data
   is Unicode, stored as glui32 arrays. */
struct data_raw_struct {
    RawType type;

    /* If this object is contained in a struct, key/keylen is the key
       (of the parent struct) that refers to it. */
    glui32 *key;
    int keylen;

    glsi32 number;
    double realnumber;
    glui32 *str;
    data_raw_t **list;
    int count;
    int allocsize;
};

static data_raw_t *data_raw_blockread(void);
static data_raw_t *data_raw_blockread_sub(char *termchar);

/* While parsing JSON, we need a place to stash strings as they come in.
   Here are a couple of resizable character buffers. */
static char *stringbuf = NULL;
static int stringbuf_size = 0;
static glui32 *ustringbuf = NULL;
static int ustringbuf_size = 0;

void gli_initialize_datainput()
{
    stringbuf_size = 64;
    stringbuf = malloc(stringbuf_size * sizeof(char));
    if (!stringbuf)
        gli_fatal_error("data: Unable to allocate memory for string buffer");

    ustringbuf_size = 64;
    ustringbuf = malloc(ustringbuf_size * sizeof(glui32));
    if (!ustringbuf)
        gli_fatal_error("data: Unable to allocate memory for string buffer");
}

static int parse_hex_digit(char ch)
{
    if (ch == EOF)
        gli_fatal_error("data: Unexpected end of input");
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch + 10 - 'A';
    if (ch >= 'a' && ch <= 'f')
        return ch + 10 - 'a';
    gli_fatal_error("data: Not a hex digit");
    return 0;
}

static char *name_for_style(short val)
{
    switch (val) {
        case style_Normal:
            return "normal";
        case style_Emphasized:
            return "emphasized";
        case style_Preformatted:
            return "preformatted";
        case style_Header:
            return "header";
        case style_Subheader:
            return "subheader";
        case style_Alert:
            return "alert";
        case style_Note:
            return "note";
        case style_BlockQuote:
            return "blockquote";
        case style_Input:
            return "input";
        case style_User1:
            return "user1";
        case style_User2:
            return "user2";
        default:
            return "unknown";
    }
}

static void ensure_stringbuf_size(int val)
{
    if (val <= stringbuf_size)
        return;
    stringbuf_size = val*2;
    stringbuf = realloc(stringbuf, stringbuf_size * sizeof(char));
    if (!stringbuf)
        gli_fatal_error("data: Unable to allocate memory for ustring buffer");
}

static void ensure_ustringbuf_size(int val)
{
    if (val <= ustringbuf_size)
        return;
    ustringbuf_size = val*2;
    ustringbuf = realloc(ustringbuf, ustringbuf_size * sizeof(glui32));
    if (!ustringbuf)
        gli_fatal_error("data: Unable to allocate memory for ustring buffer");
}

/* Send a Unicode string to an output stream, validly JSON-encoded.
   This includes the delimiting double-quotes. */
void print_ustring_json(glui32 *buf, glui32 len, FILE *fl)
{
    int ix;

    fprintf(fl, "\"");
    for (ix=0; ix<len; ix++) {
        glui32 ch = buf[ix];
        if (ch == '\"')
            fprintf(fl, "\\\"");
        else if (ch == '\\')
            fprintf(fl, "\\\\");
        else if (ch == '\n')
            fprintf(fl, "\\n");
        else if (ch == '\t')
            fprintf(fl, "\\t");
        else if (ch < 32)
            fprintf(fl, "\\u%04X", ch);
        else
            gli_putchar_utf8(ch, fl);
    }
    fprintf(fl, "\"");
}

/* Send a Latin-1 string to an output stream, validly JSON-encoded.
   This includes the delimiting double-quotes. */
void print_string_json(char *buf, FILE *fl)
{
    char *cx;

    fprintf(fl, "\"");
    for (cx=buf; *cx; cx++) {
        glui32 ch = (*cx) & 0xFF;
        if (ch == '\"')
            fprintf(fl, "\\\"");
        else if (ch == '\\')
            fprintf(fl, "\\\\");
        else if (ch == '\n')
            fprintf(fl, "\\n");
        else if (ch == '\t')
            fprintf(fl, "\\t");
        else if (ch < 32)
            fprintf(fl, "\\u%04X", ch);
        else
            gli_putchar_utf8(ch, fl);
    }
    fprintf(fl, "\"");
}

/* Send a UTF-8 string to an output stream, validly JSON-encoded.
   (This does not check that the argument is valid UTF-8; that's the
   caller's responsibility!)
   This includes the delimiting double-quotes. */
void print_utf8string_json(char *buf, FILE *fl)
{
    char *cx;

    fprintf(fl, "\"");
    for (cx=buf; *cx; cx++) {
        glui32 ch = (*cx) & 0xFF;
        if (ch == '\"')
            fprintf(fl, "\\\"");
        else if (ch == '\\')
            fprintf(fl, "\\\\");
        else if (ch == '\n')
            fprintf(fl, "\\n");
        else if (ch == '\t')
            fprintf(fl, "\\t");
        else if (ch < 32)
            fprintf(fl, "\\u%04X", ch);
        else
            fputc(ch, fl);
    }
    fprintf(fl, "\"");
}

void gen_list_init(gen_list_t *list)
{
    list->list = NULL;
    list->count = 0;
    list->allocsize = 0;
}

void gen_list_free(gen_list_t *list)
{
    if (list->list) {
        free(list->list);
        list->list = NULL;
    }
    list->count = 0;
    list->allocsize = 0;
}

void gen_list_append(gen_list_t *list, void *val)
{
    if (!list->list) {
        list->allocsize = 4;
        list->list = malloc(list->allocsize * sizeof(void *));
    }
    else {
        if (list->count >= list->allocsize) {
            list->allocsize *= 2;
            list->list = realloc(list->list, list->allocsize * sizeof(void *));
        }
    }

    if (!list->list)
        gli_fatal_error("data: Unable to allocate memory for list buffer");

    list->list[list->count++] = val;
}

static data_raw_t *data_raw_alloc(RawType type)
{
    data_raw_t *dat = malloc(sizeof(data_raw_t));
    if (!dat)
        gli_fatal_error("data: Unable to allocate memory for data block");

    dat->type = type;
    dat->key = NULL;
    dat->keylen = 0;
    dat->number = 0;
    dat->realnumber = 0.0;
    dat->str = NULL;
    dat->list = NULL;
    dat->count = 0;
    dat->allocsize = 0;
    return dat;
}

static void data_raw_free(data_raw_t *dat) 
{
    if (dat->str)
        free(dat->str);
    if (dat->key)
        free(dat->key);
    if (dat->list) {
        int ix;
        for (ix=0; ix<dat->count; ix++) {
            data_raw_free(dat->list[ix]);
        }
        free(dat->list);
    }
    dat->type = rawtyp_None;
    free(dat);
}

static void data_raw_ensure_size(data_raw_t *dat, int size)
{
    if (size <= dat->allocsize)
        return;

    if (!dat->list) {
        dat->allocsize = (size+1) * 2;
        dat->list = malloc(sizeof(data_raw_t *) * dat->allocsize);
    }
    else {
        dat->allocsize = (size+1) * 2;
        dat->list = realloc(dat->list, sizeof(data_raw_t *) * dat->allocsize);
    }

    if (!dat->list)
        gli_fatal_error("data: Unable to allocate memory for data list");
}

void data_raw_print(data_raw_t *dat)
{
    int ix;

    if (!dat) {
        printf("null");
        return;
    }

    switch (dat->type) {
        case rawtyp_Number:
            /* We don't need to output floats. */
            printf("%ld", (long)dat->number);
            return;
        case rawtyp_True:
            printf("true");
            return;
        case rawtyp_False:
            printf("false");
            return;
        case rawtyp_Null:
            printf("null");
            return;
        case rawtyp_Str:
            print_ustring_json(dat->str, dat->count, stdout);
            return;
        case rawtyp_List:
            printf("[ ");
            for (ix=0; ix<dat->count; ix++) {
                data_raw_print(dat->list[ix]);
                if (ix != dat->count-1)
                    printf(", ");
                else
                    printf(" ");
            }
            printf("]");
            return;
        case rawtyp_Struct:
            printf("{ ");
            for (ix=0; ix<dat->count; ix++) {
                data_raw_t *subdat = dat->list[ix];
                print_ustring_json(subdat->key, subdat->keylen, stdout);
                printf(": ");
                data_raw_print(subdat);
                if (ix != dat->count-1)
                    printf(", ");
                else
                    printf(" ");
            }
            printf("}");
            return;
        default:
            printf("null");
            return;
    }
}

/* Read one JSON data object from stdin. If there is none, or if the
   object is incomplete, this blocks and waits for an object to finish. */
static data_raw_t *data_raw_blockread()
{
    char termchar;

    data_raw_t *dat = data_raw_blockread_sub(&termchar);
    if (!dat)
        gli_fatal_error("data: Unexpected end of data object");

    return dat;
}

/* Validate that the object is a number, and get its value (as an int). */
static glsi32 data_raw_int_value(data_raw_t *dat)
{
    if (dat->type != rawtyp_Number)
        gli_fatal_error("data: Need number");

    return dat->number;
}

/* Validate that the object is a number, and get its value (as a real). */
static double data_raw_real_value(data_raw_t *dat)
{
    if (dat->type != rawtyp_Number)
        gli_fatal_error("data: Need number");

    return dat->realnumber;
}

/* Validate that the object is a string, and return its value as a
   freshly-malloced Unicode array. (Not null-terminated! The caller
   must record the length separately.) */
static glui32 *data_raw_str_dup(data_raw_t *dat)
{
    glui32 *str;

    if (dat->type != rawtyp_Str)
        gli_fatal_error("data: Need str");

    if (dat->count == 0) {
        /* Allocate a tiny block, because I never trusted malloc(0) */
        str = (glui32 *)malloc(1 * sizeof(glui32));
        str[0] = 0;
        return str;
    }

    str = (glui32 *)malloc(dat->count * sizeof(glui32));
    memcpy(str, dat->str, dat->count * sizeof(glui32));
    return str;
}

/* Validate that the object is a string, and return its value considered as
   a Unicode character. Special character names ("return", "escape", etc)
   are returned as Glk special character codes (keycode_Return, 
   keycode_Escape, etc.) */
static glui32 data_raw_str_char(data_raw_t *dat)
{
    if (dat->type != rawtyp_Str)
        gli_fatal_error("data: Need str");

    if (dat->count == 0) 
        gli_fatal_error("data: Need nonempty string");

    /* Check for special character names. */
    if (dat->count > 1) {
        strint_t *pair;
        for (pair = special_char_table; pair->str; pair++) {
            int pos;
            char *cx;
            for (pos=0, cx=pair->str; *cx && pos<dat->count; pos++, cx++) {
                if (dat->str[pos] != (glui32)(*cx)) {
                    break;
                }
            }
            if (*cx == '\0' && pos == dat->count)
                return pair->val;
        }
    }

    return dat->str[0];
}

/* Check whether the object is a string matching a given string. */
static int data_raw_string_is(data_raw_t *dat, char *key)
{
    char *cx;
    int pos;

    if (dat->type != rawtyp_Str)
        gli_fatal_error("data: Need str");

    for (pos=0, cx=key; *cx && pos<dat->count; pos++, cx++) {
        if (dat->str[pos] != (glui32)(*cx))
            break;
    }

    if (*cx == '\0' && pos == dat->count)
        return TRUE;
    else
        return FALSE;
}

/* Validate that the object is a struct, and return the field matching
   a given key. If there is no such field, returns NULL. */
static data_raw_t *data_raw_struct_field(data_raw_t *dat, char *key)
{
    int ix;
    char *cx;
    int pos;

    if (dat->type != rawtyp_Struct)
        gli_fatal_error("data: Need struct");

    for (ix=0; ix<dat->count; ix++) {
        data_raw_t *subdat = dat->list[ix];
        for (pos=0, cx=key; *cx && pos<subdat->keylen; pos++, cx++) {
            if (subdat->key[pos] != (glui32)(*cx))
                break;
        }
        if (*cx == '\0' && pos == subdat->keylen) {
            return subdat;
        }
    }

    return NULL;
}

/* Internal method: read a JSON element from stdin. If this sees
   a close-brace or close-bracket, it returns NULL and stores the
   character in *termchar. */
static data_raw_t *data_raw_blockread_sub(char *termchar)
{
    int ch;

    *termchar = '\0';

    while (isspace(ch = getchar())) { };
    if (ch == EOF)
        gli_fatal_error("data: Unexpected end of input");
    
    if (ch == ']' || ch == '}') {
        *termchar = ch;
        return NULL;
    }

    if (ch >= '0' && ch <= '9') {
        /* This accepts "01", which it really shouldn't, but whatever.
           We also ignore the decimal part if found, which means we're
           rounding towards zero. */
        data_raw_t *dat = data_raw_alloc(rawtyp_Number);
        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getchar();
        }

        if (ch == '.') {
            /* We have to think about real numbers. */
            ch = getchar();
            long numer = 0;
            long denom = 1;
            while (ch >= '0' && ch <= '9') {
                numer = 10 * numer + (ch-'0');
                denom *= 10;
                ch = getchar();
            }
            dat->realnumber = (double)dat->number + (double)numer / (double)denom;
        }
        else {
            dat->realnumber = (double)dat->number;
        }

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (ch == '-') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Number);
        ch = getchar();
        if (!(ch >= '0' && ch <= '9'))
            gli_fatal_error("data: minus must be followed by number");

        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getchar();
        }
        dat->number = -dat->number;

        if (ch == '.') {
            /* We have to think about real numbers. */
            ch = getchar();
            long numer = 0;
            long denom = 1;
            while (ch >= '0' && ch <= '9') {
                numer = 10 * numer + (ch-'0');
                denom *= 10;
                ch = getchar();
            }
            dat->realnumber = (double)dat->number - (double)numer / (double)denom;
        }
        else {
            dat->realnumber = (double)dat->number;
        }

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (ch == '"') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Str);

        int ix;
        int ucount = 0;
        int count = 0;
        while ((ch = getchar()) != '"') {
            if (ch == EOF)
                gli_fatal_error("data: Unterminated string");
            if (ch >= 0 && ch < 32)
                gli_fatal_error("data: Control character in string");
            if (ch == '\\') {
                ensure_ustringbuf_size(ucount + 2*count + 1);
                ucount += gli_parse_utf8((unsigned char *)stringbuf, count, ustringbuf+ucount, 2*count);
                count = 0;
                ch = getchar();
                if (ch == EOF)
                    gli_fatal_error("data: Unterminated backslash escape");
                if (ch == 'u') {
                    glui32 val = 0;
                    ch = getchar();
                    val = 16*val + parse_hex_digit(ch);
                    ch = getchar();
                    val = 16*val + parse_hex_digit(ch);
                    ch = getchar();
                    val = 16*val + parse_hex_digit(ch);
                    ch = getchar();
                    val = 16*val + parse_hex_digit(ch);
                    ustringbuf[ucount++] = val;
                    continue;
                }
                ensure_stringbuf_size(count+1);
                switch (ch) {
                    case '"':
                        stringbuf[count++] = '"';
                        break;
                    case '/':
                        stringbuf[count++] = '/';
                        break;
                    case '\\':
                        stringbuf[count++] = '\\';
                        break;
                    case 'b':
                        stringbuf[count++] = '\b';
                        break;
                    case 'f':
                        stringbuf[count++] = '\f';
                        break;
                    case 'n':
                        stringbuf[count++] = '\n';
                        break;
                    case 'r':
                        stringbuf[count++] = '\r';
                        break;
                    case 't':
                        stringbuf[count++] = '\t';
                        break;
                    default:
                        gli_fatal_error("data: Unknown backslash code");
                }
            }
            else {
                ensure_stringbuf_size(count+1);
                stringbuf[count++] = ch;
            }
        }

        ensure_ustringbuf_size(ucount + 2*count);
        ucount += gli_parse_utf8((unsigned char *)stringbuf, count, ustringbuf+ucount, 2*count);

        dat->count = ucount;
        dat->str = malloc(ucount * sizeof(glui32));
        for (ix=0; ix<ucount; ix++) {
            dat->str[ix] = ustringbuf[ix];
        }

        return dat;
    }

    if (isalpha(ch)) {
        data_raw_t *dat = NULL;

        int count = 0;
        while (isalnum(ch) || ch == '_') {
            ensure_stringbuf_size(count+1);
            stringbuf[count++] = ch;
            ch = getchar();
        }

        ensure_stringbuf_size(count+1);
        stringbuf[count++] = '\0';

        if (!strcmp(stringbuf, "true"))
            dat = data_raw_alloc(rawtyp_True);
        else if (!strcmp(stringbuf, "false"))
            dat = data_raw_alloc(rawtyp_False);
        else if (!strcmp(stringbuf, "null"))
            dat = data_raw_alloc(rawtyp_Null);
        else
            gli_fatal_error("data: Unrecognized symbol");

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (ch == '[') {
        data_raw_t *dat = data_raw_alloc(rawtyp_List);
        int count = 0;
        int commapending = FALSE;
        char term = '\0';

        while (TRUE) {
            data_raw_t *subdat = data_raw_blockread_sub(&term);
            if (!subdat) {
                if (term == ']') {
                    if (commapending)
                        gli_fatal_error("data: List should not end with comma");
                    break;
                }
                gli_fatal_error("data: Mismatched end of list");
            }
            data_raw_ensure_size(dat, count+1);
            dat->list[count++] = subdat;
            commapending = FALSE;

            while (isspace(ch = getchar())) { };
            if (ch == ']')
                break;
            if (ch != ',')
                gli_fatal_error("data: Expected comma in list");
            commapending = TRUE;
        }

        dat->count = count;
        return dat;
    }

    if (ch == '{') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Struct);
        int count = 0;
        int commapending = FALSE;
        char term = '\0';

        while (TRUE) {
            data_raw_t *keydat = data_raw_blockread_sub(&term);
            if (!keydat) {
                if (term == '}') {
                    if (commapending)
                        gli_fatal_error("data: Struct should not end with comma");
                    break;
                }
                gli_fatal_error("data: Mismatched end of struct");
            }

            if (keydat->type != rawtyp_Str)
                gli_fatal_error("data: Struct key must be string");

            while (isspace(ch = getchar())) { };
            
            if (ch != ':')
                gli_fatal_error("data: Expected colon in struct");

            data_raw_t *subdat = data_raw_blockread_sub(&term);
            if (!keydat)
                gli_fatal_error("data: Mismatched end of struct");

            subdat->key = keydat->str;
            subdat->keylen = keydat->count;
            keydat->str = NULL;
            keydat->count = 0;
            data_raw_free(keydat);
            keydat = NULL;

            data_raw_ensure_size(dat, count+1);
            dat->list[count++] = subdat;
            commapending = FALSE;

            while (isspace(ch = getchar())) { };
            if (ch == '}')
                break;
            if (ch != ',')
                gli_fatal_error("data: Expected comma in struct");
            commapending = TRUE;
        }

        dat->count = count;
        return dat;
    }

    gli_fatal_error("data: Invalid character in data");
    return NULL;
}


/* All the rest of this file is methods to allocate, free, parse, and 
   output the high-level data structures. */
   

data_metrics_t *data_metrics_alloc(int width, int height)
{
    data_metrics_t *metrics = (data_metrics_t *)malloc(sizeof(data_metrics_t));
    metrics->width = width;
    metrics->height = height;
    metrics->outspacingx = 0;
    metrics->outspacingy = 0;
    metrics->inspacingx = 0;
    metrics->inspacingy = 0;
    metrics->gridcharwidth = 1.0;
    metrics->gridcharheight = 1.0;
    metrics->gridmarginx = 0;
    metrics->gridmarginy = 0;
    metrics->buffercharwidth = 1.0;
    metrics->buffercharheight = 1.0;
    metrics->buffermarginx = 0;
    metrics->buffermarginy = 0;
    metrics->graphicsmarginx = 0;
    metrics->graphicsmarginy = 0;

    return metrics;
}

void data_metrics_free(data_metrics_t *metrics)
{
    metrics->width = 0;
    metrics->height = 0;
    free(metrics);
}

static data_metrics_t *data_metrics_parse(data_raw_t *rawdata)
{
    data_raw_t *dat;
    data_metrics_t *metrics = data_metrics_alloc(0, 0);

    if (rawdata->type != rawtyp_Struct)
        gli_fatal_error("data: Need struct");

    dat = data_raw_struct_field(rawdata, "width");
    if (!dat)
        gli_fatal_error("data: Metrics require width");
    metrics->width = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "height");
    if (!dat)
        gli_fatal_error("data: Metrics require height");
    metrics->height = data_raw_int_value(dat);

    /* charwidth/charheight aren't spec, but we accept them as a shortcut.
       (Don't send both charwidth and gridcharwidth, e.g., because the
       library might ignore the wrong one.) */
    dat = data_raw_struct_field(rawdata, "charwidth");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->gridcharwidth = val;
        metrics->buffercharwidth = val;
    }
    dat = data_raw_struct_field(rawdata, "charheight");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->gridcharheight = val;
        metrics->buffercharheight = val;
    }

    dat = data_raw_struct_field(rawdata, "gridcharwidth");
    if (dat)
        metrics->gridcharwidth = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "gridcharheight");
    if (dat)
        metrics->gridcharheight = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "buffercharwidth");
    if (dat)
        metrics->buffercharwidth = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "buffercharheight");
    if (dat)
        metrics->buffercharheight = data_raw_real_value(dat);

    dat = data_raw_struct_field(rawdata, "margin");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->gridmarginx = val;
        metrics->gridmarginy = val;
        metrics->buffermarginx = val;
        metrics->buffermarginy = val;
        metrics->graphicsmarginx = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "gridmargin");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->gridmarginx = val;
        metrics->gridmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "buffermargin");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->buffermarginx = val;
        metrics->buffermarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "graphicsmargin");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->graphicsmarginx = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "marginx");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->gridmarginx = val;
        metrics->buffermarginx = val;
        metrics->graphicsmarginx = val;
    }

    dat = data_raw_struct_field(rawdata, "marginy");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->gridmarginy = val;
        metrics->buffermarginy = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "gridmarginx");
    if (dat) 
        metrics->gridmarginx = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "gridmarginy");
    if (dat) 
        metrics->gridmarginy = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "buffermarginx");
    if (dat) 
        metrics->buffermarginx = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "buffermarginy");
    if (dat) 
        metrics->buffermarginy = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "graphicsmarginx");
    if (dat) 
        metrics->graphicsmarginx = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "graphicsmarginy");
    if (dat) 
        metrics->graphicsmarginy = data_raw_int_value(dat);

    dat = data_raw_struct_field(rawdata, "spacing");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->inspacingx = val;
        metrics->inspacingy = val;
        metrics->outspacingx = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "inspacing");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->inspacingx = val;
        metrics->inspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "outspacing");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->outspacingx = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "spacingx");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->inspacingx = val;
        metrics->outspacingx = val;
    }

    dat = data_raw_struct_field(rawdata, "spacingy");
    if (dat) {
        glsi32 val = data_raw_int_value(dat);
        metrics->inspacingy = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "inspacingx");
    if (dat)
        metrics->inspacingx = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "inspacingy");
    if (dat)
        metrics->inspacingy = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "outspacingx");
    if (dat)
        metrics->outspacingx = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "outspacingy");
    if (dat)
        metrics->outspacingy = data_raw_int_value(dat);

    if (metrics->gridcharwidth <= 0 || metrics->gridcharheight <= 0
        || metrics->buffercharwidth <= 0 || metrics->buffercharheight <= 0)
        gli_fatal_error("Metrics character size must be positive");

    return metrics;
}

void data_metrics_print(data_metrics_t *metrics)
{
    /* This displays very verbosely, and not in JSON-readable format.
       That's okay -- the library never outputs metrics, so this is only 
       used for debugging. */
 
    printf("{\n");   
    printf("  size: %ldx%ld\n", (long)metrics->width, (long)metrics->height);
    printf("  outspacing: %ldx%ld\n", (long)metrics->outspacingx, (long)metrics->outspacingy);
    printf("  inspacing: %ldx%ld\n", (long)metrics->inspacingx, (long)metrics->inspacingy);
    printf("  gridchar: %.1fx%.1f\n", metrics->gridcharwidth, metrics->gridcharheight);
    printf("  gridmargin: %ldx%ld\n", (long)metrics->gridmarginx, (long)metrics->gridmarginy);
    printf("  bufferchar: %.1fx%.1f\n", metrics->buffercharwidth, metrics->buffercharheight);
    printf("  buffermargin: %ldx%ld\n", (long)metrics->buffermarginx, (long)metrics->buffermarginy);
    printf("  graphicsmargin: %ldx%ld\n", (long)metrics->graphicsmarginx, (long)metrics->graphicsmarginy);
    printf("}\n");   
}

data_supportcaps_t *data_supportcaps_alloc()
{
    data_supportcaps_t *supportcaps = (data_supportcaps_t *)malloc(sizeof(data_supportcaps_t));

    supportcaps->timer = FALSE;
    supportcaps->hyperlinks = FALSE;
    supportcaps->graphics = FALSE;
    supportcaps->graphicswin = FALSE;
    supportcaps->sound = FALSE;

    return supportcaps;
}

void data_supportcaps_free(data_supportcaps_t *supportcaps)
{
    free(supportcaps);
}

static data_supportcaps_t *data_supportcaps_parse(data_raw_t *rawdata)
{
    data_supportcaps_t *supportcaps = data_supportcaps_alloc();

    if (rawdata->type == rawtyp_List) {
        int ix;
        for (ix=0; ix<rawdata->count; ix++) {
            data_raw_t *dat = rawdata->list[ix];

            if (data_raw_string_is(dat, "timer"))
                supportcaps->timer = TRUE;
            if (data_raw_string_is(dat, "hyperlinks"))
                supportcaps->hyperlinks = TRUE;
            if (data_raw_string_is(dat, "graphics"))
                supportcaps->graphics = TRUE;
            if (data_raw_string_is(dat, "graphicswin"))
                supportcaps->graphicswin = TRUE;
            if (data_raw_string_is(dat, "sound"))
                supportcaps->sound = TRUE;
        }
    }

    return supportcaps;
}

void data_supportcaps_print(data_supportcaps_t *supportcaps)
{
    /* This displays very verbosely, and not in JSON-readable format. */

    printf("{\n");   
    printf("  timer: %d\n", supportcaps->timer);
    printf("  hyperlinks: %d\n", supportcaps->hyperlinks);
    printf("  graphics: %d\n", supportcaps->graphics);
    printf("  graphicswin: %d\n", supportcaps->graphicswin);
    printf("  sound: %d\n", supportcaps->sound);
    printf("}\n");   
}

void data_event_free(data_event_t *data)
{
    data->dtag = dtag_Unknown;
    if (data->linevalue) {
        free(data->linevalue);
        data->linevalue = NULL;
    }
    if (data->metrics) {
        data_metrics_free(data->metrics);
        data->metrics = NULL;
    }
    if (data->supportcaps) {
        data_supportcaps_free(data->supportcaps);
        data->supportcaps = NULL;
    }
    free(data);
}

void data_event_print(data_event_t *data)
{
    switch (data->dtag) {
        case dtag_Init:
            printf("{ \"type\": \"init\", \"gen\": %d, \"metrics\":\n",
                data->gen);
            data_metrics_print(data->metrics);
            if (data->supportcaps) {
                printf(", \"support\":\n");
                data_supportcaps_print(data->supportcaps);
            }
            printf("}\n");
            break;

        /* ### Never got around to implementing the rest of this, did I... */

        default:
            printf("{? unknown dtag %d}\n", data->dtag);
            break;
    }
}

data_event_t *data_event_read()
{
    data_raw_t *dat;

    data_raw_t *rawdata = data_raw_blockread();

    if (rawdata->type != rawtyp_Struct)
        gli_fatal_error("data: Input struct not a struct");

    dat = data_raw_struct_field(rawdata, "type");
    if (!dat)
        gli_fatal_error("data: Input struct has no type");

    data_event_t *input = (data_event_t *)malloc(sizeof(data_event_t));
    input->dtag = dtag_Unknown;
    input->gen = 0;
    input->window = 0;
    input->charvalue = 0;
    input->linevalue = NULL;
    input->linelen = 0;
    input->terminator = 0;
    input->linkvalue = 0;
    input->metrics = NULL;
    input->supportcaps = NULL;

    if (data_raw_string_is(dat, "init")) {
        input->dtag = dtag_Init;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Init input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "metrics");
        if (!dat)
            gli_fatal_error("data: Init input struct has no metrics");
        input->metrics = data_metrics_parse(dat);

        dat = data_raw_struct_field(rawdata, "support");
        if (dat) {
            input->supportcaps = data_supportcaps_parse(dat);
        }
    }
    else if (data_raw_string_is(dat, "refresh")) {
        input->dtag = dtag_Refresh;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Init input struct has no gen");
        input->gen = data_raw_int_value(dat);
    }
    else if (data_raw_string_is(dat, "arrange")) {
        input->dtag = dtag_Arrange;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Arrange input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "metrics");
        if (!dat)
            gli_fatal_error("data: Arrange input struct has no metrics");

        input->metrics = data_metrics_parse(dat);
    }
    else if (data_raw_string_is(dat, "redraw")) {
        input->dtag = dtag_Redraw;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Redraw input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "window");
        if (dat)
            input->window = data_raw_int_value(dat);
    }
    else if (data_raw_string_is(dat, "line")) {
        input->dtag = dtag_Line;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Line input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "window");
        if (!dat)
            gli_fatal_error("data: Line input struct has no window");
        input->window = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "value");
        if (!dat)
            gli_fatal_error("data: Line input struct has no value");
        input->linevalue = data_raw_str_dup(dat);
        input->linelen = dat->count;

        dat = data_raw_struct_field(rawdata, "terminator");
        if (dat)
            input->terminator = data_raw_str_char(dat);
    }
    else if (data_raw_string_is(dat, "char")) {
        input->dtag = dtag_Char;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Char input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "window");
        if (!dat)
            gli_fatal_error("data: Char input struct has no window");
        input->window = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "value");
        if (!dat)
            gli_fatal_error("data: Char input struct has no value");
        input->charvalue = data_raw_str_char(dat);
    }
    else if (data_raw_string_is(dat, "hyperlink")) {
        input->dtag = dtag_Hyperlink;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Hyperlink input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "window");
        if (!dat)
            gli_fatal_error("data: Hyperlink input struct has no window");
        input->window = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "value");
        if (!dat)
            gli_fatal_error("data: Hyperlink input struct has no value");
        input->linkvalue = data_raw_int_value(dat);
    }
    else if (data_raw_string_is(dat, "timer")) {
        input->dtag = dtag_Timer;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Timer input struct has no gen");
        input->gen = data_raw_int_value(dat);
    }
    else if (data_raw_string_is(dat, "specialresponse")) {
        input->dtag = dtag_SpecialResponse;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Special input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "response");
        if (!data_raw_string_is(dat, "fileref_prompt"))
            gli_fatal_error("data: Special input struct has unknown response type");

        dat = data_raw_struct_field(rawdata, "value");
        if (dat && dat->type == rawtyp_Str) {
            input->linevalue = data_raw_str_dup(dat);
            input->linelen = dat->count;
        }
    }
    else if (data_raw_string_is(dat, "debuginput")) {
        input->dtag = dtag_DebugInput;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Debug input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "value");
        if (!dat)
            gli_fatal_error("data: Debug input struct has no value");
        input->linevalue = data_raw_str_dup(dat);
        input->linelen = dat->count;
    }
    else {
        /* Unrecognized event type. Let it go through; glk_select will
           ignore it. */
        input->dtag = dtag_Unknown;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: ??? input struct has no gen");
        input->gen = data_raw_int_value(dat);
    }

    /*### partials support */

    return input;
}

data_update_t *data_update_alloc()
{
    data_update_t *dat = (data_update_t *)malloc(sizeof(data_update_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc update structure");

    dat->gen = 0;
    dat->usewindows = FALSE;
    dat->useinputs = FALSE;
    dat->includetimer = FALSE;
    dat->timer = 0;
    dat->disable = FALSE;
    dat->specialreq = NULL;

    gen_list_init(&dat->windows);
    gen_list_init(&dat->contents);
    gen_list_init(&dat->inputs);
    gen_list_init(&dat->debuglines);

    return dat;
}

void data_update_free(data_update_t *dat)
{
    int ix;

    if (dat->specialreq) {
        data_specialreq_free(dat->specialreq);
        dat->specialreq = NULL;
    }

    data_window_t **winlist = (data_window_t **)(dat->windows.list);
    for (ix=0; ix<dat->windows.count; ix++) {
        data_window_free(winlist[ix]);
    }

    data_content_t **contlist = (data_content_t **)(dat->contents.list);
    for (ix=0; ix<dat->contents.count; ix++) {
        data_content_free(contlist[ix]);
    }

    data_input_t **inplist = (data_input_t **)(dat->inputs.list);
    for (ix=0; ix<dat->inputs.count; ix++) {
        data_input_free(inplist[ix]);
    }

    char **debuglist = (char **)(dat->debuglines.list);
    for (ix=0; ix<dat->debuglines.count; ix++) {
        free(debuglist[ix]);
    }

    gen_list_free(&dat->windows);
    gen_list_free(&dat->contents);
    gen_list_free(&dat->inputs);
    gen_list_free(&dat->debuglines);
    free(dat);
}

void data_update_print(data_update_t *dat)
{
    int ix;

    printf("{\"type\":\"update\", \"gen\":%d", dat->gen);

    if (dat->usewindows) {
        data_window_t **winlist = (data_window_t **)(dat->windows.list);
        printf(",\n \"windows\":[\n");
        for (ix=0; ix<dat->windows.count; ix++) {
            data_window_print(winlist[ix]);
            if (ix+1 < dat->windows.count)
                printf(",");
            printf("\n");
        }
        printf(" ]");
    }

    if (dat->contents.count) {
        data_content_t **contlist = (data_content_t **)(dat->contents.list);
        printf(",\n \"content\":[\n");
        for (ix=0; ix<dat->contents.count; ix++) {
            data_content_print(contlist[ix]);
            if (ix+1 < dat->contents.count)
                printf(",");
            printf("\n");
        }
        printf(" ]");
    }

    if (dat->useinputs) {
        data_input_t **inplist = (data_input_t **)(dat->inputs.list);
        printf(",\n \"input\":[\n");
        for (ix=0; ix<dat->inputs.count; ix++) {
            data_input_print(inplist[ix]);
            if (ix+1 < dat->inputs.count)
                printf(",");
            printf("\n");
        }
        printf(" ]");
    }

    if (dat->specialreq) {
        printf(",\n \"specialinput\":\n");
        data_specialreq_print(dat->specialreq);
    }

    if (dat->includetimer) {
        printf(",\n \"timer\":");
        if (!dat->timer)
            printf("null");
        else
            printf("%d", dat->timer);
    }

    if (dat->debuglines.count) {
        char **debuglist = (char **)(dat->debuglines.list);
        printf(",\n \"debugoutput\":[\n");
        for (ix=0; ix<dat->debuglines.count; ix++) {
            print_utf8string_json(debuglist[ix], stdout);
            if (ix+1 < dat->debuglines.count)
                printf(",");
            printf("\n");
        }
        printf(" ]");
    }

    printf("}\n");
}

data_window_t *data_window_alloc(glui32 window, glui32 type, glui32 rock)
{
    data_window_t *dat = (data_window_t *)malloc(sizeof(data_window_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc window structure");

    dat->window = window;
    dat->type = type;
    dat->rock = rock;
    dat->gridwidth = 0;
    dat->gridheight = 0;
    grect_set_from_size(&dat->size, 0, 0);

    return dat;
}

void data_window_free(data_window_t *dat)
{
    dat->window = 0;
    free(dat);
}

void data_window_print(data_window_t *dat)
{
    char *typename;
    switch (dat->type) {
        case wintype_TextGrid:
            typename = "grid";
            break;
        case wintype_TextBuffer:
            typename = "buffer";
            break;
        case wintype_Graphics:
            typename = "graphics";
            break;
        default:
            typename = "unknown";
            break;
    }

    printf(" { \"id\":%d, \"type\":\"%s\", \"rock\":%d,\n", dat->window, typename, dat->rock);
    if (dat->type == wintype_TextGrid)
        printf("   \"gridwidth\":%d, \"gridheight\":%d,\n", dat->gridwidth, dat->gridheight);
    if (dat->type == wintype_Graphics)
        printf("   \"graphwidth\":%d, \"graphheight\":%d,\n", dat->gridwidth, dat->gridheight);
    printf("   \"left\":%d, \"top\":%d, \"width\":%d, \"height\":%d }",
        dat->size.left, dat->size.top, dat->size.right-dat->size.left, dat->size.bottom-dat->size.top);
}

data_input_t *data_input_alloc(glui32 window, glui32 evtype)
{
    data_input_t *dat = (data_input_t *)malloc(sizeof(data_input_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc input structure");

    dat->window = window;
    dat->evtype = evtype;
    dat->gen = 0;
    dat->initstr = NULL;
    dat->initlen = 0;
    dat->maxlen = 0;
    dat->cursorpos = FALSE;
    dat->xpos = -1;
    dat->ypos = -1;
    dat->hyperlink = FALSE;

    return dat;
}

void data_input_free(data_input_t *dat)
{
    dat->window = 0;
    dat->evtype = 0;
    dat->gen = 0;

    if (dat->initstr) {
        free(dat->initstr);
        dat->initstr = NULL;
    }

    free(dat);
}

void data_input_print(data_input_t *dat)
{
    printf(" {\"id\":%d, \"gen\":%d", dat->window, dat->gen);

    switch (dat->evtype) {
        case evtype_CharInput:
            printf(", \"type\":\"char\"");
            break;
        case evtype_LineInput:
            printf(", \"type\":\"line\", \"maxlen\":%d", dat->maxlen);
            if (dat->initstr && dat->initlen) {
                printf(", \"initial\":");
                print_ustring_json(dat->initstr, dat->initlen, stdout);
            }
            break;
    }

    if (dat->cursorpos) {
        printf(", \"xpos\":%d, \"ypos\":%d", dat->xpos, dat->ypos);
    }

    if (dat->hyperlink) {
        printf(", \"hyperlink\":true");
    }

    printf(" }");
}

data_content_t *data_content_alloc(glui32 window, glui32 type)
{
    data_content_t *dat = (data_content_t *)malloc(sizeof(data_content_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc content structure");

    dat->window = window;
    dat->type = type;
    dat->clear = FALSE;

    gen_list_init(&dat->lines);

    return dat;
}

void data_content_free(data_content_t *dat)
{
    int ix;

    data_line_t **linelist = (data_line_t **)(dat->lines.list);
    for (ix=0; ix<dat->lines.count; ix++) {
        data_line_free(linelist[ix]);
    }

    gen_list_free(&dat->lines);

    free(dat);
}

void data_content_print(data_content_t *dat)
{
    int ix;
    char *linelabel;

    if (dat->type == wintype_TextBuffer) {
        char *isclear = "";
        if (dat->clear)
            isclear = ", \"clear\":true";
        linelabel = "text";
        printf(" {\"id\":%d%s", dat->window, isclear);
    }
    else if (dat->type == wintype_TextGrid) {
        linelabel = "lines";
        printf(" {\"id\":%d", dat->window);
    }
    else if (dat->type == wintype_Graphics) {
        linelabel = "draw";
        printf(" {\"id\":%d", dat->window);
    }
    else {
        gli_fatal_error("data: Unknown window type in content_print");
    }

    if (dat->lines.count) {
        printf(", \"%s\": [\n", linelabel);

        if (dat->type != wintype_Graphics) {
            data_line_t **linelist = (data_line_t **)(dat->lines.list);
            for (ix=0; ix<dat->lines.count; ix++) {
                data_line_print(linelist[ix], dat->type);
                if (ix+1 < dat->lines.count)
                    printf(",");
                printf("\n");
            }
        }
        else {
            /* The specialspans are passed as the contents of a single
               data_line_t. */
            if (dat->lines.count == 1) {
                data_line_t *line = dat->lines.list[0];
                for (ix=0; ix<line->count; ix++) {
                    data_specialspan_print(line->spans[ix].special, dat->type);
                    if (ix+1 < line->count)
                        printf(",");
                    printf("\n");
                }
            }
        }

        printf(" ]");
    }

    printf(" }");
}

data_line_t *data_line_alloc()
{
    data_line_t *dat = (data_line_t *)malloc(sizeof(data_line_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc line structure");

    dat->append = FALSE;
    dat->flowbreak = FALSE;
    dat->linenum = 0;

    dat->spans = NULL;
    dat->count = 0;
    dat->allocsize = 0;

    return dat;
}

void data_line_free(data_line_t *dat)
{
    if (dat->spans) {
        /* Do not free the span strings or specials. */
        free(dat->spans);
        dat->spans = NULL;
    };
    dat->allocsize = 0;
    dat->count = 0;

    free(dat);
    return;
}

void data_line_add_span(data_line_t *data, short style, glui32 hyperlink, glui32 *str, long len)
{
    if (!data->spans) {
        data->allocsize = 4;
        data->spans = malloc(data->allocsize * sizeof(data_span_t));
    }
    else {
        if (data->count >= data->allocsize) {
            data->allocsize *= 2;
            data->spans = realloc(data->spans, data->allocsize * sizeof(data_span_t));
        }
    }

    if (!data->spans)
        gli_fatal_error("data: Unable to allocate memory for span buffer");

    data_span_t *span = &(data->spans[data->count++]);
    span->style = style;
    span->hyperlink = hyperlink;
    span->str = str;
    span->len = len;
    span->special = NULL;
}

void data_line_add_specialspan(data_line_t *data, data_specialspan_t *special)
{
    /* The flowbreak is a special case. It does not get added as a span;
       it just sets the flowbreak flag on the line. */
    if (special->type == specialtype_FlowBreak) {
        data->flowbreak = TRUE;
        return;
    }

    if (!data->spans) {
        data->allocsize = 4;
        data->spans = malloc(data->allocsize * sizeof(data_span_t));
    }
    else {
        if (data->count >= data->allocsize) {
            data->allocsize *= 2;
            data->spans = realloc(data->spans, data->allocsize * sizeof(data_span_t));
        }
    }

    if (!data->spans)
        gli_fatal_error("data: Unable to allocate memory for span buffer");

    data_span_t *span = &(data->spans[data->count++]);
    span->style = 0;
    span->hyperlink = 0;
    span->str = NULL;
    span->len = 0;
    span->special = special;
}

void data_line_print(data_line_t *dat, glui32 wintype)
{
    int ix;
    int any = FALSE;

    printf("  {");

    if (wintype == wintype_TextGrid) {
        printf(" \"line\":%d", dat->linenum);
        any = TRUE;
    }
    else {
        if (dat->append) {
            printf("\"append\":true");
            any = TRUE;
        }
        if (dat->flowbreak) {
            if (any)
                printf(", ");
            printf("\"flowbreak\":true");
            any = TRUE;
        }
    }

    if (dat->count) {
        if (any)
            printf(", ");

        printf("\"content\":[");
        
        for (ix=0; ix<dat->count; ix++) {
            data_span_t *span = &(dat->spans[ix]);
            if (span->special) {
                data_specialspan_print(span->special, wintype_TextBuffer);
            }
            else {
                char *stylename = name_for_style(span->style);
                printf("{ \"style\":\"%s\"", stylename);
                if (span->hyperlink)
                    printf(", \"hyperlink\":%ld", (unsigned long)span->hyperlink);
                printf(", \"text\":");
                print_ustring_json(span->str, span->len, stdout);
                printf("}");
            }
            if (ix+1 < dat->count)
                printf(", ");
        }
        
        printf("]");
    }

    printf("}");

}

data_specialspan_t *data_specialspan_alloc(SpecialType type)
{
    data_specialspan_t *dat = (data_specialspan_t *)malloc(sizeof(data_specialspan_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc specialspan structure");

    dat->type = type;
    dat->chunktype = 0;
    dat->image = 0;
    dat->hasdimensions = FALSE;
    dat->width = 0;
    dat->height = 0;
    dat->xpos = 0;
    dat->ypos = 0;
    dat->alignment = 0;
    dat->hyperlink = 0;
    dat->alttext = NULL;
    dat->hascolor = FALSE;
    dat->color = 0;

    return dat;
}

void data_specialspan_free(data_specialspan_t *dat)
{
    dat->type = specialtype_None;
    dat->alttext = NULL;
    free(dat);
    return;
}

void data_specialspan_print(data_specialspan_t *dat, glui32 wintype)
{
    /* For error cases, this prints an ordinary text span. */

    switch (dat->type) {

    case specialtype_Image:
        printf("{\"special\":\"image\", \"image\":%d, \"width\":%d, \"height\":%d", dat->image, dat->width, dat->height);
        if (wintype == wintype_Graphics)
            printf(", \"x\":%d, \"y\":%d", dat->xpos, dat->ypos);

        if (pref_resourceurl) {
            char *suffix = "";
            if (dat->chunktype == 0x4A504547)
                suffix = ".jpeg";
            else if (dat->chunktype == 0x504E4720)
                suffix = ".png";
            printf(", \"url\":\"%spict-%d%s\"", pref_resourceurl, dat->image, suffix);
        }

        if (wintype != wintype_Graphics) {
            char *alignment;
            switch (dat->alignment) {
            default:
            case imagealign_InlineUp:
                alignment = "inlineup";
                break;
            case imagealign_InlineDown:
                alignment = "inlinedown";
                break;
            case imagealign_InlineCenter:
                alignment = "inlinecenter";
                break;
            case imagealign_MarginLeft:
                alignment = "marginleft";
                break;
            case imagealign_MarginRight:
                alignment = "marginright";
                break;
            }
            printf(", \"alignment\":\"%s\"", alignment);
        }

        if (dat->hyperlink)
            printf(", \"hyperlink\":\"%d\"", dat->hyperlink);
        if (dat->alttext) {
            /* ### not sure what format the alt-text is in yet */
            printf(", \"alttext\":\"###\"");
        }
        printf("}");
        break;

    case specialtype_FlowBreak:
        printf("{\"text\":\"[ERROR: data_specialspan_print: flowbreak should have been converted to a line flag]\"}");
        break;

    case specialtype_SetColor:
        printf("{\"special\":\"setcolor\"");
        if (dat->hascolor)
            printf(", \"color\":\"#%06X\"", dat->color);
        printf("}");
        break;

    case specialtype_Fill:
        printf("{\"special\":\"fill\"");
        if (dat->hasdimensions)
            printf(", \"x\":%d, \"y\":%d", dat->xpos, dat->ypos);
        if (dat->hasdimensions)
            printf(", \"width\":%d, \"height\":%d", dat->width, dat->height);
        if (dat->hascolor)
            printf(", \"color\":\"#%06X\"", dat->color);
        printf("}");
        break;

    default:
        printf("{\"text\":\"[ERROR: data_specialspan_print: unrecognized special type]\"}");
        break;

    }
}

data_specialreq_t *data_specialreq_alloc(glui32 filemode, glui32 filetype)
{
    data_specialreq_t *dat = (data_specialreq_t *)malloc(sizeof(data_specialreq_t));
    if (!dat)
        gli_fatal_error("data: Unable to alloc specialreq structure");

    dat->filemode = filemode;
    dat->filetype = filetype;
    dat->gameid = NULL;

    return dat;
}

void data_specialreq_free(data_specialreq_t *dat)
{
    if (dat->gameid) {
        free(dat->gameid);
        dat->gameid = NULL;
    }
    free(dat);
    return;
}

void data_specialreq_print(data_specialreq_t *dat)
{
    char *filemode;
    char *filetype;

    switch (dat->filemode) {
        case filemode_Write:
            filemode = "write";
            break;
        case filemode_ReadWrite:
            filemode = "readwrite";
            break;
        case filemode_WriteAppend:
            filemode = "writeappend";
            break;
        case filemode_Read:
        default:
            filemode = "read";
            break;
    }

    switch (dat->filetype) {
        case fileusage_SavedGame:
            filetype = "save";
            break;
        case fileusage_Transcript:
            filetype = "transcript";
            break;
        case fileusage_InputRecord:
            filetype = "command";
            break;
        default:
        case fileusage_Data:
            filetype = "data";
            break;
    }

    printf("  { \"type\":\"%s\", \"filemode\":\"%s\", \"filetype\":\"%s\"", 
        "fileref_prompt", filemode, filetype);
    if (dat->gameid) {
        printf(",\n    \"gameid\":");
        print_string_json(dat->gameid, stdout);
    }
    printf(" }");
}

