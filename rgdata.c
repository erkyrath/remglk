/* rgdata.c: JSON data structure objects
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "rgdata_int.h"
#include "glkstart.h"

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

static data_raw_t *data_raw_blockread(FILE *file);
static data_raw_t *data_raw_blockread_sub(FILE *file, char *termchar);

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
void print_ustring_len_json(glui32 *buf, glui32 len, FILE *fl)
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

/* Send a Latin-1 string to an output stream, validly JSON-encoded.
   Same as above except that the string doesn't have to be null-terminated
   (and may contain nulls). */
void print_string_len_json(char *buf, int len, FILE *fl)
{
    char *cx;
    int ix;
    
    fprintf(fl, "\"");
    for (ix=0, cx=buf; ix<len; ix++, cx++) {
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
            print_ustring_len_json(dat->str, dat->count, stdout);
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
                print_ustring_len_json(subdat->key, subdat->keylen, stdout);
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

/* Read one JSON data object from the file. If there is none, or if the
   object is incomplete, this blocks and waits for an object to finish. */
static data_raw_t *data_raw_blockread(FILE *file)
{
    char termchar;

    data_raw_t *dat = data_raw_blockread_sub(file, &termchar);
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

/* Validate that the object is a boolean, and get its value. */
static int data_raw_bool_value(data_raw_t *dat)
{
    if (dat->type == rawtyp_True) {
        return TRUE;
    }
    if (dat->type == rawtyp_False) {
        return FALSE;
    }

    gli_fatal_error("data: Need bool");
    return FALSE;
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

/* Internal method: read a JSON element from the file. If this sees
   a close-brace or close-bracket, it returns NULL and stores the
   character in *termchar. */
static data_raw_t *data_raw_blockread_sub(FILE *file, char *termchar)
{
    int ch;

    *termchar = '\0';

    while (isspace(ch = getc(file))) { };
    if (ch == EOF)
        gli_fatal_error("data: Unexpected end of input");
    
    if (ch == ']' || ch == '}') {
        *termchar = ch;
        return NULL;
    }

    if ((ch >= '0' && ch <= '9') || ch == '-') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Number);
        int minus = FALSE;
        
        if (ch == '-') {
            minus = TRUE;
            ch = getc(file);
        }

        /* We accept "01" here, which is technically outside the spec. */
        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getc(file);
        }

        if (ch == '.' || ch == 'e' || ch == 'E') {
            /* We have to think about real numbers. And scientific notation, for json's sake. */
            double fval = dat->number;
            if (ch == '.') {
                ch = getc(file);
                long numer = 0;
                long numerlen = 0;
                /* We accept "1." here, which is outside the spec. */
                while (ch >= '0' && ch <= '9') {
                    numer = 10 * numer + (ch-'0');
                    numerlen++;
                    ch = getc(file);
                }
                if (numerlen) {
                    fval += numer * pow(10, -numerlen);
                }
            }

            if (ch == 'e' || ch == 'E') {
                ch = getc(file);
                int expminus = FALSE;
                /* We accept "1e", "1e+", and "1-e" here. Again, non-spec. */
                if (ch == '-') {
                    expminus = TRUE;
                    ch = getc(file);
                }
                else if (ch == '+') {
                    expminus = FALSE;
                    ch = getc(file);
                }
                int expnum = 0;
                while (ch >= '0' && ch <= '9') {
                    expnum = 10 * expnum + (ch-'0');
                    ch = getc(file);
                }
                if (expminus) {
                    expnum = -expnum;
                }
                fval = fval * pow(10, expnum);
            }

            if (minus) {
                fval = -fval;
            }

            dat->realnumber = fval;
            dat->number = round(fval);
        }
        else {
            /* Just digits; it's an integer. */
            if (minus) {
                dat->number = -dat->number;
            }
            dat->realnumber = (double)dat->number;
        }

        if (ch != EOF)
            ungetc(ch, file);
        return dat;
    }

    if (ch == '"') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Str);

        int ix;
        int ucount = 0;
        int count = 0;
        while ((ch = getc(file)) != '"') {
            if (ch == EOF)
                gli_fatal_error("data: Unterminated string");
            if (ch >= 0 && ch < 32)
                gli_fatal_error("data: Control character in string");
            if (ch == '\\') {
                ensure_ustringbuf_size(ucount + 2*count + 1);
                ucount += gli_parse_utf8((unsigned char *)stringbuf, count, ustringbuf+ucount, 2*count);
                count = 0;
                ch = getc(file);
                if (ch == EOF)
                    gli_fatal_error("data: Unterminated backslash escape");
                if (ch == 'u') {
                    glui32 val = 0;
                    ch = getc(file);
                    val = 16*val + parse_hex_digit(ch);
                    ch = getc(file);
                    val = 16*val + parse_hex_digit(ch);
                    ch = getc(file);
                    val = 16*val + parse_hex_digit(ch);
                    ch = getc(file);
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
            ch = getc(file);
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
            ungetc(ch, file);
        return dat;
    }

    if (ch == '[') {
        data_raw_t *dat = data_raw_alloc(rawtyp_List);
        int count = 0;
        int commapending = FALSE;
        char term = '\0';

        while (TRUE) {
            data_raw_t *subdat = data_raw_blockread_sub(file, &term);
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

            while (isspace(ch = getc(file))) { };
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
            data_raw_t *keydat = data_raw_blockread_sub(file, &term);
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

            while (isspace(ch = getc(file))) { };
            
            if (ch != ':')
                gli_fatal_error("data: Expected colon in struct");

            data_raw_t *subdat = data_raw_blockread_sub(file, &term);
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

            while (isspace(ch = getc(file))) { };
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

data_metrics_t *data_metrics_parse(data_raw_t *rawdata)
{
    data_raw_t *dat;
    data_metrics_t *metrics = data_metrics_alloc(0, 0);

    if (rawdata->type != rawtyp_Struct)
        gli_fatal_error("data: Need struct");

    dat = data_raw_struct_field(rawdata, "width");
    if (!dat)
        gli_fatal_error("data: Metrics require width");
    metrics->width = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "height");
    if (!dat)
        gli_fatal_error("data: Metrics require height");
    metrics->height = data_raw_real_value(dat);

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
        double val = data_raw_real_value(dat);
        metrics->gridmarginx = val;
        metrics->gridmarginy = val;
        metrics->buffermarginx = val;
        metrics->buffermarginy = val;
        metrics->graphicsmarginx = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "gridmargin");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->gridmarginx = val;
        metrics->gridmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "buffermargin");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->buffermarginx = val;
        metrics->buffermarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "graphicsmargin");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->graphicsmarginx = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "marginx");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->gridmarginx = val;
        metrics->buffermarginx = val;
        metrics->graphicsmarginx = val;
    }

    dat = data_raw_struct_field(rawdata, "marginy");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->gridmarginy = val;
        metrics->buffermarginy = val;
        metrics->graphicsmarginy = val;
    }

    dat = data_raw_struct_field(rawdata, "gridmarginx");
    if (dat) 
        metrics->gridmarginx = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "gridmarginy");
    if (dat) 
        metrics->gridmarginy = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "buffermarginx");
    if (dat) 
        metrics->buffermarginx = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "buffermarginy");
    if (dat) 
        metrics->buffermarginy = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "graphicsmarginx");
    if (dat) 
        metrics->graphicsmarginx = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "graphicsmarginy");
    if (dat) 
        metrics->graphicsmarginy = data_raw_real_value(dat);

    dat = data_raw_struct_field(rawdata, "spacing");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->inspacingx = val;
        metrics->inspacingy = val;
        metrics->outspacingx = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "inspacing");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->inspacingx = val;
        metrics->inspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "outspacing");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->outspacingx = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "spacingx");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->inspacingx = val;
        metrics->outspacingx = val;
    }

    dat = data_raw_struct_field(rawdata, "spacingy");
    if (dat) {
        double val = data_raw_real_value(dat);
        metrics->inspacingy = val;
        metrics->outspacingy = val;
    }

    dat = data_raw_struct_field(rawdata, "inspacingx");
    if (dat)
        metrics->inspacingx = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "inspacingy");
    if (dat)
        metrics->inspacingy = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "outspacingx");
    if (dat)
        metrics->outspacingx = data_raw_real_value(dat);
    dat = data_raw_struct_field(rawdata, "outspacingy");
    if (dat)
        metrics->outspacingy = data_raw_real_value(dat);

    if (metrics->gridcharwidth <= 0 || metrics->gridcharheight <= 0
        || metrics->buffercharwidth <= 0 || metrics->buffercharheight <= 0)
        gli_fatal_error("Metrics character size must be positive");

    return metrics;
}

void data_metrics_print(FILE *fl, data_metrics_t *metrics)
{
    fprintf(fl, "{\n");   
    fprintf(fl, "  \"width\": %.2f, \"height\": %.2f,\n", metrics->width, metrics->height);
    fprintf(fl, "  \"outspacingx\": %.2f, \"outspacingy\": %.2f,\n", metrics->outspacingx, metrics->outspacingy);
    fprintf(fl, "  \"inspacingx\": %.2f, \"inspacingy\": %.2f,\n", metrics->inspacingx, metrics->inspacingy);
    fprintf(fl, "  \"gridcharwidth\": %.2f, \"gridcharheight\": %.2f,\n", metrics->gridcharwidth, metrics->gridcharheight);
    fprintf(fl, "  \"gridmarginx\": %.2f, \"gridmarginy\": %.2f,\n", metrics->gridmarginx, metrics->gridmarginy);
    fprintf(fl, "  \"buffercharwidth\": %.2f, \"buffercharheight\": %.2f,\n", metrics->buffercharwidth, metrics->buffercharheight);
    fprintf(fl, "  \"buffermarginx\": %.2f, \"buffermarginy\": %.2f,\n", metrics->buffermarginx, metrics->buffermarginy);
    fprintf(fl, "  \"graphicsmarginx\": %.2f, \"graphicsmarginy\": %.2f\n", metrics->graphicsmarginx, metrics->graphicsmarginy);
    fprintf(fl, "}\n");   
}

data_supportcaps_t *data_supportcaps_alloc()
{
    data_supportcaps_t *supportcaps = (data_supportcaps_t *)malloc(sizeof(data_supportcaps_t));

    supportcaps->timer = FALSE;
    supportcaps->hyperlinks = FALSE;
    supportcaps->graphics = FALSE;
    supportcaps->graphicswin = FALSE;
    supportcaps->graphicsext = FALSE;
    supportcaps->sound = FALSE;

    return supportcaps;
}

void data_supportcaps_clear(data_supportcaps_t *supportcaps)
{
    supportcaps->timer = FALSE;
    supportcaps->hyperlinks = FALSE;
    supportcaps->graphics = FALSE;
    supportcaps->graphicswin = FALSE;
    supportcaps->graphicsext = FALSE;
    supportcaps->sound = FALSE;
}

void data_supportcaps_merge(data_supportcaps_t *supportcaps, data_supportcaps_t *other)
{
    if (other->timer)
        supportcaps->timer = TRUE;
    if (other->hyperlinks)
        supportcaps->hyperlinks = TRUE;
    if (other->graphics)
        supportcaps->graphics = TRUE;
    if (other->graphicswin)
        supportcaps->graphicswin = TRUE;
    if (other->graphicsext)
        supportcaps->graphicsext = TRUE;
    if (other->sound)
        supportcaps->sound = TRUE;
}

void data_supportcaps_free(data_supportcaps_t *supportcaps)
{
    free(supportcaps);
}

data_supportcaps_t *data_supportcaps_parse(data_raw_t *rawdata)
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
            if (data_raw_string_is(dat, "graphicsext"))
                supportcaps->graphicsext = TRUE;
            if (data_raw_string_is(dat, "sound"))
                supportcaps->sound = TRUE;
        }
    }

    return supportcaps;
}

void data_supportcaps_print(FILE *fl, data_supportcaps_t *supportcaps)
{
    int any = FALSE;
    
    fprintf(fl, "[");
    if (supportcaps->timer) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"timer\"");
        any = TRUE;
    }
    if (supportcaps->hyperlinks) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"hyperlinks\"");
        any = TRUE;
    }
    if (supportcaps->graphics) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"graphics\"");
        any = TRUE;
    }
    if (supportcaps->graphicswin) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"graphicswin\"");
        any = TRUE;
    }
    if (supportcaps->graphicsext) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"graphicsext\"");
        any = TRUE;
    }
    if (supportcaps->sound) {
        if (any) fprintf(fl, ", ");
        fprintf(fl, "\"sound\"");
        any = TRUE;
    }
    fprintf(fl, "]\n");   
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
            data_metrics_print(stdout, data->metrics);
            if (data->supportcaps) {
                printf(", \"support\":\n");
                data_supportcaps_print(stdout, data->supportcaps);
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

    data_raw_t *rawdata = data_raw_blockread(stdin);

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
    input->mousex = 0;
    input->mousey = 0;
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
    else if (data_raw_string_is(dat, "mouse")) {
        input->dtag = dtag_Mouse;

        dat = data_raw_struct_field(rawdata, "gen");
        if (!dat)
            gli_fatal_error("data: Mouse input struct has no gen");
        input->gen = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "window");
        if (!dat)
            gli_fatal_error("data: Mouse input struct has no window");
        input->window = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "x");
        if (!dat)
            gli_fatal_error("data: Mouse input struct has no x value");
        input->mousex = data_raw_int_value(dat);

        dat = data_raw_struct_field(rawdata, "y");
        if (!dat)
            gli_fatal_error("data: Mouse input struct has no y value");
        input->mousey = data_raw_int_value(dat);
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

        /* This "value" field will be a string if we are connected
           to RegTest, but a dialog.js fileref object if we are connected
           to GlkOte. */
        dat = data_raw_struct_field(rawdata, "value");
        if (dat && dat->type == rawtyp_Str) {
            input->linevalue = data_raw_str_dup(dat);
            input->linelen = dat->count;
        }
        else if (dat && dat->type == rawtyp_Struct) {
            data_raw_t *subdat = data_raw_struct_field(dat, "filename");
            if (subdat && subdat->type == rawtyp_Str) {
                input->linevalue = data_raw_str_dup(subdat);
                input->linelen = subdat->count;
            }
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
    dat->mouse = FALSE;

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
                print_ustring_len_json(dat->initstr, dat->initlen, stdout);
            }
            break;
    }

    if (dat->cursorpos) {
        printf(", \"xpos\":%d, \"ypos\":%d", dat->xpos, dat->ypos);
    }

    if (dat->hyperlink) {
        printf(", \"hyperlink\":true");
    }

    if (dat->mouse) {
        printf(", \"mouse\":true");
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
                print_ustring_len_json(span->str, span->len, stdout);
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
    dat->xpos = 0;
    dat->ypos = 0;
    dat->width = 0;
    dat->height = 0;
    dat->widthratio = 0.0;
    dat->aspectwidth = 0.0;
    dat->aspectheight = 0.0;
    dat->winmaxwidth = 0.0;
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
        printf("{\"special\":\"image\", \"image\":%d", dat->image);
        
        if (wintype == wintype_Graphics) {
            printf(", \"width\":%d, \"height\":%d", dat->width, dat->height);
            printf(", \"x\":%d, \"y\":%d", dat->xpos, dat->ypos);
        }
        else {
            if (dat->width)
                printf(", \"width\":%d", dat->width);
            if (dat->height)
                printf(", \"height\":%d", dat->height);
            if (dat->widthratio)
                printf(", \"widthratio\":%.4f", dat->widthratio);
            if (dat->aspectwidth)
                printf(", \"aspectwidth\":%.2f", dat->aspectwidth);
            if (dat->aspectheight)
                printf(", \"aspectheight\":%.2f", dat->aspectheight);
            if (dat->winmaxwidth) {
                if (dat->winmaxwidth < 0.0)
                    printf(", \"winmaxwidth\":null");
                else
                    printf(", \"winmaxwidth\":%.4f", dat->winmaxwidth);
            }
        }

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

/* Complete dump for autosave. */
void data_specialspan_auto_print(FILE *file, data_specialspan_t *dat)
{
    fprintf(file, "{\"type\":%d", (int)dat->type);

    if (dat->image)
        fprintf(file, ", \"image\":%ld", (long)dat->image);
    if (dat->chunktype)
        fprintf(file, ", \"chunktype\":%ld", (long)dat->chunktype);
    if (dat->hasdimensions)
        fprintf(file, ", \"hasdimensions\":true");

    if (dat->xpos || dat->ypos)
        fprintf(file, ", \"xpos\":%ld, \"ypos\":%ld", (long)dat->xpos, (long)dat->ypos);
    if (dat->width || dat->height)
        fprintf(file, ", \"width\":%ld, \"height\":%ld", (long)dat->width, (long)dat->height);
    if (dat->widthratio)
        fprintf(file, ", \"widthratio\":%.4f", dat->widthratio);
    if (dat->aspectwidth)
        fprintf(file, ", \"aspectwidth\":%.4f", dat->aspectwidth);
    if (dat->aspectheight)
        fprintf(file, ", \"aspectheight\":%.4f", dat->aspectheight);
    /* negative winmaxwidth is stored as-is, not as "null" */
    if (dat->winmaxwidth)
        fprintf(file, ", \"winmaxwidth\":%.4f", dat->winmaxwidth);
    if (dat->alignment)
        fprintf(file, ", \"alignment\":%ld", (long)dat->alignment);
    if (dat->hyperlink)
        fprintf(file, ", \"hyperlink\":%ld", (long)dat->hyperlink);

    if (dat->alttext) {
        fprintf(file, ", \"alttext\":");
        print_string_json(dat->alttext, file);
    }
    
    if (dat->hascolor)
        fprintf(file, ", \"hascolor\":true");
    if (dat->color)
        fprintf(file, ", \"color\":%ld", (long)dat->color);

    fprintf(file, "}");
}

data_specialspan_t *data_specialspan_auto_parse(data_raw_t *rawdata)
{
    int ix;
    data_raw_t *dat;

    dat = data_raw_struct_field(rawdata, "type");
    if (!dat)
        return NULL;
    
    int typeval = data_raw_int_value(dat);
    data_specialspan_t *special = data_specialspan_alloc(typeval);
    if (!special)
        return NULL;

    dat = data_raw_struct_field(rawdata, "image");
    if (dat)
        special->image = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "chunktype");
    if (dat)
        special->chunktype = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "hasdimensions");
    if (dat) {
        if (dat->type == rawtyp_Number)
            special->hasdimensions = data_raw_int_value(dat);
        else
            special->hasdimensions = data_raw_bool_value(dat);
    }
    dat = data_raw_struct_field(rawdata, "xpos");
    if (dat)
        special->xpos = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "ypos");
    if (dat)
        special->ypos = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "width");
    if (dat)
        special->width = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "height");
    //###
    if (dat)
        special->height = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "alignment");
    if (dat)
        special->alignment = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "hyperlink");
    if (dat)
        special->hyperlink = data_raw_int_value(dat);

    dat = data_raw_struct_field(rawdata, "alttext");
    if (dat && dat->type == rawtyp_Str) {
        /* This gets leaked; the library doesn't expect it to be alloced. */
        special->alttext = malloc(dat->count+1);
        for (ix=0; ix<dat->count; ix++) {
            glui32 ch = dat->str[ix];
            special->alttext[ix] = ((ch < 0x100) ? (char)ch : '?');
        }
        special->alttext[dat->count] = '\0';
    }

    dat = data_raw_struct_field(rawdata, "hascolor");
    if (dat) {
        if (dat->type == rawtyp_Number)
            special->hascolor = data_raw_int_value(dat);
        else
            special->hascolor = data_raw_bool_value(dat);
    }
    dat = data_raw_struct_field(rawdata, "color");
    if (dat)
        special->color = data_raw_int_value(dat);

    return special;
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

data_tempbufinfo_t *data_tempbufinfo_alloc()
{
    data_tempbufinfo_t *temp = (data_tempbufinfo_t *)malloc(sizeof(data_tempbufinfo_t));
    if (!temp)
        return NULL;

    temp->bufdata = NULL;
    temp->ubufdata = NULL;
    temp->bufdatalen = 0;
    temp->bufkey = 0;
    temp->bufptr = 0;
    temp->bufend = 0;
    temp->bufeof = 0;

    return temp;
}

void data_tempbufinfo_free(data_tempbufinfo_t *temp)
{
    if (temp->bufdata) {
        free(temp->bufdata);
        temp->bufdata = NULL;
    }
    if (temp->ubufdata) {
        free(temp->ubufdata);
        temp->ubufdata = NULL;
    }
    
    free(temp);
}

void data_grect_clear(grect_t *box)
{
    box->left = 0;
    box->top = 0;
    box->right = 0;
    box->bottom = 0;
}

void data_grect_print(FILE *file, grect_t *box)
{
    fprintf(file, "{\"left\":%d, \"top\":%d, \"right\":%d, \"bottom\":%d}",
        box->left, box->top, box->right, box->bottom);
}

void data_grect_parse(data_raw_t *rawdata, grect_t *box)
{
    data_raw_t *dat;

    grect_set_from_size(box, 0, 0);

    dat = data_raw_struct_field(rawdata, "left");
    if (dat)
        box->left = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "top");
    if (dat)
        box->top = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "right");
    if (dat)
        box->right = data_raw_int_value(dat);
    dat = data_raw_struct_field(rawdata, "bottom");
    if (dat)
        box->bottom = data_raw_int_value(dat);
}

glkunix_library_state_t glkunix_library_state_alloc()
{
    glkunix_library_state_t state = (glkunix_library_state_t)malloc(sizeof(struct glkunix_library_state_struct));
    if (!state)
        gli_fatal_error("data: unable to alloc library_state");
    
    memset(state, 0, sizeof(struct glkunix_library_state_struct));

    state->metrics = NULL;
    state->supportcaps = NULL;
    state->windowlist = NULL;
    state->streamlist = NULL;
    state->filereflist = NULL;

    state->rootwin = NULL;
    state->currentstr = NULL;

    return state;
}

void glkunix_library_state_free(glkunix_library_state_t state)
{
    int ix;
    
    if (state->metrics) {
        data_metrics_free(state->metrics);
        state->metrics = NULL;
    }

    if (state->supportcaps) {
        data_supportcaps_free(state->supportcaps);
        state->supportcaps = NULL;
    }

    if (state->windowlist) {
        for (ix=0; ix<state->windowcount; ix++) {
            winid_t win = state->windowlist[ix];
            if (win) {
                gli_window_dealloc_inactive(win);
            }
        }
        free(state->windowlist);
        state->windowlist = NULL;
    }
    
    if (state->streamlist) {
        for (ix=0; ix<state->streamcount; ix++) {
            strid_t str = state->streamlist[ix];
            if (str) {
                gli_stream_dealloc_inactive(str);
            }
        }
        free(state->streamlist);
        state->streamlist = NULL;
    }
    
    if (state->filereflist) {
        for (ix=0; ix<state->filerefcount; ix++) {
            frefid_t fref = state->filereflist[ix];
            if (fref) {
                gli_fileref_dealloc_inactive(fref);
            }
        }
        free(state->filereflist);
        state->filereflist = NULL;
    }

    state->rootwin = NULL;
    state->currentstr = NULL;
    
    free(state);
}

void glkunix_serialize_object_root(FILE *file, glkunix_serialize_context_t ctx, glkunix_serialize_object_f func, void *rock)
{
    ctx->file = file;
    ctx->count = 0;

    fprintf(ctx->file, "{");
    func(ctx, rock);
    fprintf(ctx->file, "}");
}

void glkunix_serialize_uint32(glkunix_serialize_context_t ctx, char *key, glui32 val)
{
    if (ctx->count) {
        fprintf(ctx->file, ", ");
    }

    fprintf(ctx->file, "\"%s\":%ld", key, (long)val);

    ctx->count++;
}

void glkunix_serialize_object(glkunix_serialize_context_t ctx, char *key, glkunix_serialize_object_f func, void *rock)
{
    if (ctx->count) {
        fprintf(ctx->file, ", ");
    }

    fprintf(ctx->file, "\"%s\":{", key);
    func(ctx, rock);
    fprintf(ctx->file, "}");

    ctx->count++;
}

void glkunix_serialize_object_list(glkunix_serialize_context_t ctx, char *key, glkunix_serialize_object_f func, int count, size_t size, void *array)
{
    char *charray = array;
    int ix;
    
    if (ctx->count) {
        fprintf(ctx->file, ", ");
    }

    fprintf(ctx->file, "\"%s\":[\n", key);
    
    for (ix=0; ix<count; ix++) {
        char *el = charray + ix*size;
        struct glkunix_serialize_context_struct subctx;
        
        if (ix > 0) {
            fprintf(ctx->file, ",\n");
        }

        glkunix_serialize_object_root(ctx->file, &subctx, func, el);
    }
    
    fprintf(ctx->file, "\n]");

    ctx->count++;
}

static glkunix_unserialize_context_t glkunix_unserialize_context_alloc()
{
    glkunix_unserialize_context_t ctx = (glkunix_unserialize_context_t)malloc(sizeof(struct glkunix_unserialize_context_struct));
    ctx->dat = NULL;
    ctx->subctx = NULL;
    return ctx;
}

int glkunix_unserialize_object_root(FILE *file, glkunix_unserialize_context_t ctx)
{
    ctx->dat = NULL;
    ctx->subctx = NULL;
    
    ctx->dat = data_raw_blockread(file);
    if (!ctx->dat)
        return FALSE;

    return TRUE;
}

void glkunix_unserialize_object_root_finalize(glkunix_unserialize_context_t ctx)
{
    /* We free the chain of subctx structures, but not the dat pointers -- those are all references to elements of ctx->dat. */
    while (ctx->subctx) {
        glkunix_unserialize_context_t tmp = ctx->subctx->subctx;
        free(ctx->subctx);
        ctx->subctx = tmp;
    }

    if (ctx->dat) {
        data_raw_free(ctx->dat);
        ctx->dat = NULL;
    }
}

int glkunix_unserialize_uint32(glkunix_unserialize_context_t ctx, char *key, glui32 *res)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;
    
    glsi32 val = data_raw_int_value(dat);
    *res = (glui32)val;
    return TRUE;
}

int glkunix_unserialize_int(glkunix_unserialize_context_t ctx, char *key, int *res)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;
    
    glsi32 val = data_raw_int_value(dat);
    *res = (int)val;
    return TRUE;
}

int glkunix_unserialize_long(glkunix_unserialize_context_t ctx, char *key, long *res)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;
    
    glsi32 val = data_raw_int_value(dat);
    *res = (long)val;
    return TRUE;
}

int glkunix_unserialize_real(glkunix_unserialize_context_t ctx, char *key, double *res)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;
    
    *res = data_raw_real_value(dat);
    return TRUE;
}

/* Returned (null-terminated) string is malloced */
int glkunix_unserialize_latin1_string(glkunix_unserialize_context_t ctx, char *key, char **res)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;

    if (dat->type != rawtyp_Str)
        gli_fatal_error("data: Need str");

    int ix;
    
    char *buf = malloc(dat->count+1);
    for (ix=0; ix<dat->count; ix++) {
        glui32 ch = dat->str[ix];
        buf[ix] = ((ch < 0x100) ? (char)ch : '?');
    }
    buf[dat->count] = '\0';
    *res = buf;
    return TRUE;
}

/* Returned string is malloced */
int glkunix_unserialize_len_bytes(glkunix_unserialize_context_t ctx, char *key, unsigned char **res, long *reslen)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;

    if (dat->type != rawtyp_Str)
        gli_fatal_error("data: Need str");

    unsigned char *buf;
    
    if (dat->count == 0) {
        buf = malloc(1);
        buf[0] = 0;
    }
    else {
        int ix;
        buf = malloc(dat->count);
        for (ix=0; ix<dat->count; ix++) {
            glui32 ch = dat->str[ix];
            buf[ix] = (ch & 0xFF);
        }
    }
    *res = buf;
    *reslen = dat->count;
    return TRUE;
}

/* Returned unicode-string is malloced */
int glkunix_unserialize_len_unicode(glkunix_unserialize_context_t ctx, char *key, glui32 **res, long *reslen)
{
    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;

    *res = data_raw_str_dup(dat);
    *reslen = dat->count;
    return TRUE;
}

int glkunix_unserialize_struct(glkunix_unserialize_context_t ctx, char *key, glkunix_unserialize_context_t *subctx)
{
    *subctx = NULL;

    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;

    if (dat->type != rawtyp_Struct)
        return FALSE;

    if (!ctx->subctx) {
        ctx->subctx = glkunix_unserialize_context_alloc();
    }
    
    ctx->subctx->dat = dat;
    *subctx = ctx->subctx;
    return TRUE;
}

int glkunix_unserialize_list(glkunix_unserialize_context_t ctx, char *key, glkunix_unserialize_context_t *subctx, int *count)
{
    *subctx = NULL;

    data_raw_t *dat = data_raw_struct_field(ctx->dat, key);
    if (!dat)
        return FALSE;

    if (dat->type != rawtyp_List)
        return FALSE;

    if (!ctx->subctx) {
        ctx->subctx = glkunix_unserialize_context_alloc();
    }
    
    ctx->subctx->dat = dat;
    *subctx = ctx->subctx;
    *count = dat->count;
    return TRUE;
}

int glkunix_unserialize_list_entry(glkunix_unserialize_context_t ctx, int pos, glkunix_unserialize_context_t *subctx)
{
    *subctx = NULL;

    if (!ctx->dat)
        return FALSE;
    if (ctx->dat->type != rawtyp_List)
        return FALSE;
    if (!ctx->dat->list || pos < 0 || pos >= ctx->dat->count)
        return FALSE;

    if (!ctx->subctx) {
        ctx->subctx = glkunix_unserialize_context_alloc();
    }
    
    ctx->subctx->dat = ctx->dat->list[pos];
    *subctx = ctx->subctx;
    return TRUE;
}

int glkunix_unserialize_uint32_list_entry(glkunix_unserialize_context_t ctx, int pos, glui32 *res)
{
    if (!ctx->dat)
        return FALSE;
    if (ctx->dat->type != rawtyp_List)
        return FALSE;
    if (!ctx->dat->list || pos < 0 || pos >= ctx->dat->count)
        return FALSE;

    glsi32 val = data_raw_int_value(ctx->dat->list[pos]);
    *res = (glui32)val;
    return TRUE;
}

int glkunix_unserialize_object_list_entries(glkunix_unserialize_context_t ctx, glkunix_unserialize_object_f func, int count, size_t size, void *array)
{
    char *charray = array;
    int ix;

    if (!ctx->dat)
        return FALSE;
    if (ctx->dat->type != rawtyp_List)
        return FALSE;
    if (!ctx->dat->list || count > ctx->dat->count)
        return FALSE;

    if (!ctx->subctx) {
        ctx->subctx = glkunix_unserialize_context_alloc();
    }

    for (ix=0; ix<count; ix++) {
        char *el = charray + ix*size;
        ctx->subctx->dat = ctx->dat->list[ix];
        if (!func(ctx->subctx, el))
            return FALSE;
    }

    ctx->subctx->dat = NULL;

    return TRUE;
}

