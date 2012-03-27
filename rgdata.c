
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
    rawtyp_Symbol = 5,
} RawType;

typedef struct data_raw_struct data_raw_t;

struct data_raw_struct {
    RawType type;
    char *key;

    glui32 number;
    char *str;
    data_raw_t **list;
    int count;
    int allocsize;
};

static data_raw_t *data_raw_blockread(void);
static data_raw_t *data_raw_blockread_sub(char *termchar);

static char *stringbuf = NULL;
static int stringbuf_size = 0;

void gli_initialize_datainput()
{
    stringbuf_size = 64;
    stringbuf = malloc(stringbuf_size * sizeof(char));
    if (!stringbuf)
        gli_fatal_error("data: Unable to allocate memory for string buffer");
}

static void ensure_stringbuf_size(int val)
{
    if (val <= stringbuf_size)
        return;
    stringbuf_size = val*2;
    stringbuf = realloc(stringbuf, stringbuf_size *sizeof(char));
    if (!stringbuf)
        gli_fatal_error("data: Unable to allocate memory for string buffer");
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

static void data_raw_dump(data_raw_t *dat)
{
    char *cx;
    int ix;

    if (!dat) {
        printf("<?NULL>");
        return;
    }

    switch (dat->type) {
        case rawtyp_Int:
            printf("%ld", (long)dat->number);
            return;
        case rawtyp_Symbol:
            printf("%s", dat->str);
            return;
        case rawtyp_Str:
            printf("'");
            for (cx=dat->str; *cx; cx++) {
                if (*cx == '\'')
                    printf("\\'");
                else if (*cx < 32)
                    printf("\\x%2x", *cx);
                else
                    printf("%c", *cx);
            }
            printf("'");
            return;
        case rawtyp_List:
            printf("[ ");
            for (ix=0; ix<dat->count; ix++) {
                data_raw_dump(dat->list[ix]);
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
                data_raw_dump(dat->list[ix]);
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
    if (dat->type == rawtyp_Symbol)
        gli_fatal_error("data: Unexpected symbol in data");

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
        data_raw_t *dat = data_raw_alloc(rawtyp_Int);
        while (ch >= '0' && ch <= '9') {
            dat->number = 10 * dat->number + (ch-'0');
            ch = getchar();
        }

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (isalpha(ch) || ch == '_') {
        data_raw_t *dat = data_raw_alloc(rawtyp_Symbol);
        int count = 0;
        while (isalnum(ch) || ch == '_') {
            ensure_stringbuf_size(count+1);
            stringbuf[count++] = ch;
            ch = getchar();
        }

        ensure_stringbuf_size(count+1);
        stringbuf[count++] = '\0';
        dat->str = strdup(stringbuf);

        if (ch != EOF)
            ungetc(ch, stdin);
        return dat;
    }

    if (ch == '[') {
        data_raw_t *dat = data_raw_alloc(rawtyp_List);
        int count = 0;
        char term = '\0';
        while (TRUE) {
            data_raw_t *subdat = data_raw_blockread_sub(&term);
            if (!subdat) {
                if (term == ']')
                    break;
                gli_fatal_error("data: Mismatched end of array");
            }
            if (subdat->type == rawtyp_Symbol)
                gli_fatal_error("data: Unexpected symbol in data");
            data_raw_ensure_size(dat, count+1);
            dat->list[count++] = subdat;

            while (isspace(ch = getchar())) { };
            if (ch == ']')
                break;
            if (ch != ',')
                gli_fatal_error("data: Expected comma in list");
        }
        dat->count = count;
        return dat;
    }

    gli_fatal_error("data: Invalid character in data");
    return NULL;
}

data_input_t *data_input_read()
{
    data_raw_t *rawdata = data_raw_blockread();

    data_raw_dump(rawdata); printf("\n"); /*###*/

    return NULL; /*###*/
}
