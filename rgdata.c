
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "glk.h"
#include "remglk.h"
#include "rgdata.h"

typedef enum RawType_enum {
    rawtyp_None = 0,
    rawtyp_Int = 1,
    rawtyp_Str = 2,
    rawtyp_List = 3,
    rawtyp_Struct = 4,
    rawtyp_True = 5,
    rawtyp_False = 6,
    rawtyp_Null = 7,
} RawType;

typedef struct data_raw_struct data_raw_t;

struct data_raw_struct {
    RawType type;
    char *key;

    glsi32 number;
    glui32 *str;
    data_raw_t **list;
    int count;
    int allocsize;
};

static data_raw_t *data_raw_blockread(void);
static data_raw_t *data_raw_blockread_sub(char *termchar);

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

static data_raw_t *data_raw_alloc(RawType type)
{
    data_raw_t *dat = malloc(sizeof(data_raw_t));
    if (!dat)
        gli_fatal_error("data: Unable to allocate memory for data block");

    dat->type = type;
    dat->key = NULL;
    dat->number = 0;
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
        printf("<?NULL>");
        return;
    }

    switch (dat->type) {
        case rawtyp_Int:
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
            printf("\"");
            for (ix=0; ix<dat->count; ix++) {
                glui32 ch = dat->str[ix];
                if (ch == '\"')
                    printf("\\\"");
                else if (ch == '\\')
                    printf("\\\\");
                else if (ch == '\n')
                    printf("\\n");
                else if (ch == '\t')
                    printf("\\t");
                else if (ch < 32)
                    printf("\\u%04X", ch);
                else
                    gli_putchar_utf8(ch, stdout);
            }
            printf("\"");
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
                printf("%s: ", dat->key);
                data_raw_print(dat->list[ix]);
                if (ix != dat->count-1)
                    printf(", ");
                else
                    printf(" ");
            }
            printf("}");
            return;
        default:
            printf("<?>");
            return;
    }
}

static data_raw_t *data_raw_blockread()
{
    char termchar;

    data_raw_t *dat = data_raw_blockread_sub(&termchar);
    if (!dat)
        gli_fatal_error("data: Unexpected end of data object");

    return dat;
}

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
        /* This accepts "01", which it really shouldn't, but whatever */
        data_raw_t *dat = data_raw_alloc(rawtyp_Int);
        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getchar();
        }

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (ch == '-') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Int);
        ch = getchar();
        if (!(ch >= '0' && ch <= '9'))
            gli_fatal_error("data: minus must be followed by number");

        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getchar();
        }
        dat->number = -dat->number;

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
                        gli_fatal_error("data: Array should not end with comma");
                    break;
                }
                gli_fatal_error("data: Mismatched end of array");
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

    gli_fatal_error("data: Invalid character in data");
    return NULL;
}

void data_metrics_print(data_metrics_t *metrics)
{
    /* This displays very verbosely, and not in JSON-readable format.
       That's okay -- it's only used for debugging. */
 
    printf("{\n");   
    printf("  size: %ldx%ld\n", (long)metrics->width, (long)metrics->height);
    printf("  outspacing: %ldx%ld\n", (long)metrics->outspacingx, (long)metrics->outspacingy);
    printf("  inspacing: %ldx%ld\n", (long)metrics->inspacingx, (long)metrics->inspacingy);
    printf("  gridchar: %ldx%ld\n", (long)metrics->gridcharwidth, (long)metrics->gridcharheight);
    printf("  gridmargin: %ldx%ld\n", (long)metrics->gridmarginx, (long)metrics->gridmarginy);
    printf("  bufferchar: %ldx%ld\n", (long)metrics->buffercharwidth, (long)metrics->buffercharheight);
    printf("  buffermargin: %ldx%ld\n", (long)metrics->buffermarginx, (long)metrics->buffermarginy);
    printf("}\n");   
}

void data_input_print(data_input_t *data)
{
    switch (data->dtag) {
        case dtag_Init:
            printf("{ \"type\": \"init\", \"gen\": %d, \"metrics\":\n",
                data->gen);
            data_metrics_print(data->metrics);
            printf("}\n");
            break;

        default:
            printf("{? unknown dtag %d}\n", data->dtag);
            break;
    }
}

data_input_t *data_input_read()
{
    data_raw_t *rawdata = data_raw_blockread();

    return NULL; /*###*/
}
