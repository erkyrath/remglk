/* main.c: Top-level source file
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Glk API which this implements: version 0.7.6.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"
#include "rgdata.h"
#include "glkstart.h"

/* Declarations of preferences flags. */
static int pref_printversion = FALSE;
int pref_stderr = FALSE;
int pref_fixedmetrics = FALSE;
int pref_autometrics = FALSE;
int pref_singleturn = FALSE;
static int pref_screenwidth = 80;
static int pref_screenheight = 50;
static data_supportcaps_t pref_supportcaps;
char *pref_resourceurl = NULL;
#if GIDEBUG_LIBRARY_SUPPORT
int gli_debugger = FALSE;
#endif /* GIDEBUG_LIBRARY_SUPPORT */

typedef struct dataresource_struct {
    int num;
    int isbinary;
    char *pathname;
    int len;
    void *ptr;
} dataresource_t;
static dataresource_t *dataresources = NULL;
static int numdataresources = 0, dataresource_size = 0;

/* Some constants for my wacky little command-line option parser. */
#define ex_Void (0)
#define ex_Int (1)
#define ex_Bool (2)
#define ex_Str (3)

static int errflag = FALSE;
static int inittime = FALSE;

static int extract_value(int argc, char *argv[], char *optname, int type,
    int *argnum, int *result, int defval);
static int string_to_bool(char *str);
static char *construct_resourceurl(char *str, int ispath);
static int add_dataresource(char *progname, char *str, int isbinary);

#define STRBUFLEN (512)
static char extracted_string[STRBUFLEN];

int main(int argc, char *argv[])
{
    int ix, jx, val;
    glkunix_startup_t startdata;
    
    /* Test for compile-time errors. If one of these spouts off, you
        must edit glk.h and recompile. */
    if (sizeof(glui32) != 4) {
        printf("Compile-time error: glui32 is not a 32-bit value. Please fix glk.h.\n");
        return 1;
    }
    if ((glui32)(-1) < 0) {
        printf("Compile-time error: glui32 is not unsigned. Please fix glk.h.\n");
        return 1;
    }

    data_supportcaps_clear(&pref_supportcaps);
    
    /* Now some argument-parsing. This is probably going to hurt. */
    startdata.argc = 0;
    startdata.argv = (char **)malloc(argc * sizeof(char *));
    
    /* Copy in the program name. */
    startdata.argv[startdata.argc] = argv[0];
    startdata.argc++;
    
    for (ix=1; ix<argc && !errflag; ix++) {
        glkunix_argumentlist_t *argform;
        int inarglist = FALSE;
        char *cx;
        
        for (argform = glkunix_arguments; 
            argform->argtype != glkunix_arg_End && !errflag; 
            argform++) {
            
            if (argform->name[0] == '\0') {
                if (argv[ix][0] != '-') {
                    startdata.argv[startdata.argc] = argv[ix];
                    startdata.argc++;
                    inarglist = TRUE;
                }
            }
            else if ((argform->argtype == glkunix_arg_NumberValue)
                && !strncmp(argv[ix], argform->name, strlen(argform->name))
                && (cx = argv[ix] + strlen(argform->name))
                && (atoi(cx) != 0 || cx[0] == '0')) {
                startdata.argv[startdata.argc] = argv[ix];
                startdata.argc++;
                inarglist = TRUE;
            }
            else if (!strcmp(argv[ix], argform->name)) {
                int numeat = 0;
                
                if (argform->argtype == glkunix_arg_ValueFollows) {
                    if (ix+1 >= argc) {
                        printf("%s: %s must be followed by a value\n", 
                            argv[0], argform->name);
                        errflag = TRUE;
                        break;
                    }
                    numeat = 2;
                }
                else if (argform->argtype == glkunix_arg_NoValue) {
                    numeat = 1;
                }
                else if (argform->argtype == glkunix_arg_ValueCanFollow) {
                    if (ix+1 < argc && argv[ix+1][0] != '-') {
                        numeat = 2;
                    }
                    else {
                        numeat = 1;
                    }
                }
                else if (argform->argtype == glkunix_arg_NumberValue) {
                    if (ix+1 >= argc
                        || (atoi(argv[ix+1]) == 0 && argv[ix+1][0] != '0')) {
                        printf("%s: %s must be followed by a number\n", 
                            argv[0], argform->name);
                        errflag = TRUE;
                        break;
                    }
                    numeat = 2;
                }
                else {
                    errflag = TRUE;
                    break;
                }
                
                for (jx=0; jx<numeat; jx++) {
                    startdata.argv[startdata.argc] = argv[ix];
                    startdata.argc++;
                    if (jx+1 < numeat)
                        ix++;
                }
                inarglist = TRUE;
                break;
            }
        }
        if (inarglist || errflag)
            continue;
            
        if (argv[ix][0] != '-') {
            printf("%s: unwanted argument: %s\n", argv[0], argv[ix]);
            errflag = TRUE;
            break;
        }
        
        if (extract_value(argc, argv, "?", ex_Void, &ix, &val, FALSE))
            errflag = TRUE;
        else if (extract_value(argc, argv, "help", ex_Void, &ix, &val, FALSE))
            errflag = TRUE;
        else if (extract_value(argc, argv, "version", ex_Void, &ix, &val, FALSE))
            pref_printversion = val;
        else if (extract_value(argc, argv, "v", ex_Void, &ix, &val, FALSE))
            pref_printversion = val;
        else if (extract_value(argc, argv, "fixmetrics", ex_Bool, &ix, &val, FALSE))
            pref_fixedmetrics = val;
        else if (extract_value(argc, argv, "fm", ex_Bool, &ix, &val, FALSE))
            pref_fixedmetrics = val;
        else if (extract_value(argc, argv, "autometrics", ex_Bool, &ix, &val, FALSE))
            pref_autometrics = val;
        else if (extract_value(argc, argv, "am", ex_Bool, &ix, &val, FALSE))
            pref_autometrics = val;
        else if (extract_value(argc, argv, "width", ex_Int, &ix, &val, 80))
            pref_screenwidth = val;
        else if (extract_value(argc, argv, "w", ex_Int, &ix, &val, 80))
            pref_screenwidth = val;
        else if (extract_value(argc, argv, "height", ex_Int, &ix, &val, 50))
            pref_screenheight = val;
        else if (extract_value(argc, argv, "h", ex_Int, &ix, &val, 50))
            pref_screenheight = val;
        else if (extract_value(argc, argv, "singleturn", ex_Bool, &ix, &val, FALSE))
            pref_singleturn = val;
        else if (extract_value(argc, argv, "st", ex_Bool, &ix, &val, FALSE))
            pref_singleturn = val;
        else if (extract_value(argc, argv, "stderr", ex_Bool, &ix, &val, FALSE))
            pref_stderr = val;
        else if (extract_value(argc, argv, "support", ex_Str, &ix, &val, FALSE)) {
            if (!strcmp(extracted_string, "timer") || !strcmp(extracted_string, "timers"))
                pref_supportcaps.timer = TRUE;
            else if (!strcmp(extracted_string, "hyperlink") || !strcmp(extracted_string, "hyperlinks"))
                pref_supportcaps.hyperlinks = TRUE;
            else if (!strcmp(extracted_string, "graphics"))
                pref_supportcaps.graphics = TRUE;
            else if (!strcmp(extracted_string, "graphicswin"))
                pref_supportcaps.graphicswin = TRUE;
            else if (!strcmp(extracted_string, "graphicsext"))
                pref_supportcaps.graphicsext = TRUE;
            else {
                printf("%s: -support value not recognized: %s\n", argv[0], extracted_string);
                errflag = TRUE;
            }
        }
        else if (extract_value(argc, argv, "resourcedir", ex_Str, &ix, &val, FALSE)) 
            pref_resourceurl = construct_resourceurl(extracted_string, TRUE);
        else if (extract_value(argc, argv, "rd", ex_Str, &ix, &val, FALSE)) 
            pref_resourceurl = construct_resourceurl(extracted_string, TRUE);
        else if (extract_value(argc, argv, "resourceurl", ex_Str, &ix, &val, FALSE)) 
            pref_resourceurl = construct_resourceurl(extracted_string, FALSE);
        else if (extract_value(argc, argv, "ru", ex_Str, &ix, &val, FALSE)) 
            pref_resourceurl = construct_resourceurl(extracted_string, FALSE);
        else if (extract_value(argc, argv, "dataresourcebin", ex_Str, &ix, &val, FALSE)) {
            if (!add_dataresource(argv[0], extracted_string, TRUE))
                errflag = TRUE;
        }
        else if (extract_value(argc, argv, "dataresourcetext", ex_Str, &ix, &val, FALSE)) {
            if (!add_dataresource(argv[0], extracted_string, FALSE))
                errflag = TRUE;
        }
        else if (extract_value(argc, argv, "dataresource", ex_Str, &ix, &val, FALSE)) {
            if (!add_dataresource(argv[0], extracted_string, TRUE))
                errflag = TRUE;
        }
#if GIDEBUG_LIBRARY_SUPPORT
        else if (extract_value(argc, argv, "D", ex_Void, &ix, &val, FALSE))
            gli_debugger = val;
#endif /* GIDEBUG_LIBRARY_SUPPORT */
        else {
            printf("%s: unknown option: %s\n", argv[0], argv[ix]);
            errflag = TRUE;
        }
    }
    
    if (errflag) {
        printf("usage: %s [ options ... ]\n", argv[0]);
        if (glkunix_arguments[0].argtype != glkunix_arg_End) {
            glkunix_argumentlist_t *argform;
            printf("game options:\n");
            for (argform = glkunix_arguments; 
                argform->argtype != glkunix_arg_End; 
                argform++) {
                if (strlen(argform->name) == 0)
                    printf("  %s\n", argform->desc);
                else if (argform->argtype == glkunix_arg_ValueFollows)
                    printf("  %s val: %s\n", argform->name, argform->desc);
                else if (argform->argtype == glkunix_arg_NumberValue)
                    printf("  %s val: %s\n", argform->name, argform->desc);
                else if (argform->argtype == glkunix_arg_ValueCanFollow)
                    printf("  %s [val]: %s\n", argform->name, argform->desc);
                else
                    printf("  %s: %s\n", argform->name, argform->desc);
            }
        }
        printf("library options:\n");
        printf("  -fixmetrics BOOL: define screen size manually (default 'no')\n");
        printf("  -autometrics BOOL: allow screen size to be set during autorestore (default 'no')\n");
        printf("  -width NUM: manual screen width (default 80)\n");
        printf("  -height NUM: manual screen height (default 50)\n");
        printf("  -support [timer, hyperlinks, graphics, graphicswin, graphicsext]: declare support for various input features\n");
        printf("  -resourceurl STR: URL base for image/sound files\n");
        printf("  -resourcedir STR: path to image/sound files (used to create file: URLs)\n");
        printf("  -dataresource NUM:PATHNAME, -dataresourcebin NUM:PATHNAME, -dataresourcetext NUM:PATHNAME: tell where the data resource file with the given number can be read (default: search blorb if available)\n");
        printf("     (file is considered binary by default, or text if -dataresourcetext is used)\n");
        printf("  -singleturn BOOL: exit the process after responding to one input (default 'no')\n");
        printf("  -stderr BOOL: send errors to stderr rather than stdout (default 'no')\n");
#if GIDEBUG_LIBRARY_SUPPORT
        printf("  -D: turn on debug console\n");
#endif /* GIDEBUG_LIBRARY_SUPPORT */
        printf("  -version: display Glk library version\n");
        printf("  -help: display this list\n");
        printf("NUM values can be any number. BOOL values can be 'yes' or 'no', or no value to toggle.\n");
        return 1;
    }
    
    if (pref_printversion) {
        printf("RemGlk, library version %s.\n", LIBRARY_VERSION);
        printf("For more information, see http://eblong.com/zarf/glk/\n");
        return 1;
    }

    /* Initialize things. */
    gli_initialize_datainput();
    gli_initialize_misc(&pref_supportcaps);
    gli_initialize_windows();
    gli_initialize_streams();
    gli_initialize_filerefs();
    gli_initialize_events();

    inittime = TRUE;
    if (!glkunix_startup_code(&startdata)) {
        glk_exit();
    }
    inittime = FALSE;

    if (!pref_autometrics) {
        data_metrics_t *metrics = data_metrics_alloc(pref_screenwidth, pref_screenheight);
        
        if (!pref_fixedmetrics) {
            data_supportcaps_t newcaps;
            gli_select_metrics(metrics, &newcaps);
            data_supportcaps_merge(&gli_supportcaps, &newcaps);
        }
        else {
            gli_select_imaginary();
        }

        gli_windows_update_metrics(metrics);
        
        data_metrics_free(metrics);
    }

    if (gli_debugger)
        gidebug_announce_cycle(gidebug_cycle_Start);

    /* Call the program main entry point, and then exit. */
    glk_main();
    glk_exit();
    
    /* glk_exit() doesn't return, but the compiler may kvetch if main()
        doesn't seem to return a value. */
    return 0;
}

/* This is my own parsing system for command-line options. It's nothing
    special, but it works. 
   Given argc and argv, check to see if option argnum matches the string
    optname. If so, parse its value according to the type flag. Store the
    result in result if it matches, and return TRUE; return FALSE if it
    doesn't match. argnum is a pointer so that it can be incremented in
    cases like "-width 80". defval is the default value, which is only
    meaningful for boolean options (so that just "-ml" can toggle the
    value of the ml option.) */
static int extract_value(int argc, char *argv[], char *optname, int type,
    int *argnum, int *result, int defval)
{
    int optlen, val;
    char *cx, *origcx;
    
    optlen = strlen(optname);
    origcx = argv[*argnum];
    cx = origcx;
    
    /* Skip initial dash */
    cx++;
    
    if (strncmp(cx, optname, optlen))
        return FALSE;
    
    cx += optlen;
    
    switch (type) {
    
        case ex_Void:
            if (*cx)
                return FALSE;
            *result = TRUE;
            return TRUE;
    
        case ex_Int:
            if (*cx == '\0') {
                if ((*argnum)+1 >= argc) {
                    cx = "";
                }
                else {
                    (*argnum) += 1;
                    cx = argv[*argnum];
                }
            }
            val = atoi(cx);
            if (val == 0 && cx[0] != '0') {
                printf("%s: %s must be followed by a number\n", 
                    argv[0], origcx);
                errflag = TRUE;
                return FALSE;
            }
            *result = val;
            return TRUE;

        case ex_Bool:
            if (*cx == '\0') {
                if ((*argnum)+1 >= argc) {
                    val = -1;
                }
                else {
                    char *cx2 = argv[(*argnum)+1];
                    val = string_to_bool(cx2);
                    if (val != -1)
                        (*argnum) += 1;
                }
            }
            else {
                val = string_to_bool(cx);
                if (val == -1) {
                    printf("%s: %s must be followed by a boolean value\n", 
                        argv[0], origcx);
                    errflag = TRUE;
                    return FALSE;
                }
            }
            if (val == -1)
                val = !defval;
            *result = val;
            return TRUE;

        case ex_Str:
            if (*cx == '\0') {
                if ((*argnum)+1 >= argc) {
                    cx = "";
                }
                else {
                    (*argnum) += 1;
                    cx = argv[*argnum];
                }
            }
            strncpy(extracted_string, cx, STRBUFLEN-1);
            extracted_string[STRBUFLEN-1] = '\0';
            *result = 1;
            return TRUE;
            
    }
    
    return FALSE;
}

static int string_to_bool(char *str)
{
    if (!strcmp(str, "y") || !strcmp(str, "yes"))
        return TRUE;
    if (!strcmp(str, "n") || !strcmp(str, "no"))
        return FALSE;
    if (!strcmp(str, "on"))
        return TRUE;
    if (!strcmp(str, "off"))
        return FALSE;
    if (!strcmp(str, "+"))
        return TRUE;
    if (!strcmp(str, "-"))
        return FALSE;
        
    return -1;
}

/* Given an argument NUM:PATHNAME from the command line, add an entry
   to the dataresources array. */
static int add_dataresource(char *progname, char *str, int isbinary)
{
    if (!strlen(str)) {
        printf("%s: -dataresource option requires NUM:PATHNAME\n\n", progname);
        return FALSE;
    }
    char *sep = strchr(str, ':');
    if (!sep || sep == str || *(sep+1) == '\0') {
        printf("%s: -dataresource option requires NUM:PATHNAME\n\n", progname);
        return FALSE;
    }
    *sep = '\0';
    sep++;
    int val = atoi(str);
    if (!dataresources || dataresource_size == 0) {
        dataresource_size = 4;
        dataresources = (dataresource_t *)malloc(dataresource_size * sizeof(dataresource_t));
    }
    else if (numdataresources >= dataresource_size) {
        dataresource_size *= 2;
        dataresources = (dataresource_t *)realloc(dataresources, dataresource_size * sizeof(dataresource_t));
    }
    dataresources[numdataresources].num = val;
    dataresources[numdataresources].isbinary = isbinary;
    dataresources[numdataresources].pathname = strdup(sep);
    dataresources[numdataresources].ptr = NULL;
    dataresources[numdataresources].len = 0;
    numdataresources++;
    return TRUE;
}

/* Given a path or URL (taken from the resourcedir/resourceurl argument),
   return a (malloced) string containing a URL form. If ispath is
   true, the path is absolutized and turned into a file: URL. */
static char *construct_resourceurl(char *str, int ispath)
{
    char *res = NULL;

    if (!ispath) {
        /* We don't append a slash here, because maybe the user wants
           URLs like http://foo/prefix-pict-1.png. */
        res = malloc(strlen(str) + 1);
        if (!res)
            return NULL;
        strcpy(res, str);
    }
    else {
        /* This assumes Unix-style pathnames. Sorry. */
        char prefix[STRBUFLEN];
        prefix[0] = '\0';
        int len = strlen(str);
        int preslash = FALSE;
        if (len && str[0] != '/') {
            getcwd(prefix, STRBUFLEN);
            preslash = TRUE;
        }
        int postslash = FALSE;
        if (len && str[len-1] != '/')
            postslash = TRUE;
        res = malloc(16 + strlen(prefix) + len + 1);
        if (!res)
            return NULL;
        sprintf(res, "file://%s%s%s%s", prefix, (preslash?"/":""), str, (postslash?"/":""));
    }

    return res;
}

/* Get the data for data chunk num (as specified in command-line arguments,
   if any).
   The data is read from the given pathname and stashed in memory.
   This is memory-hoggish, but so is the rest of glk_stream_open_resource();
   see comments there.
   (You might wonder why we don't call gli_stream_open_pathname() and
   handle the file as a file-based stream. Turns out that doesn't work;
   the handling of unicode streams is subtly different for resource
   streams and the file-based code won't work. Oh well.)
*/
int gli_get_dataresource_info(int num, void **ptr, glui32 *len, int *isbinary)
{
    int ix;
    /* The dataresources array isn't sorted (or even checked for duplicates),
       so we search it linearly. There probably aren't a lot of entries. */
    for (ix=0; ix<numdataresources; ix++) {
        if (dataresources[ix].num == num) {
            *isbinary = dataresources[ix].isbinary;
            *ptr = NULL;
            *len = 0;
            if (dataresources[ix].ptr) {
                /* Already loaded. */
            }
            else {
                FILE *fl = fopen(dataresources[ix].pathname, "rb");
                if (!fl) {
                    gli_strict_warning("stream_open_resource: unable to read given pathname.");
                    return FALSE;
                }
                fseek(fl, 0, SEEK_END);
                dataresources[ix].len = ftell(fl);
                if (dataresources[ix].len < 0) {
                    gli_strict_warning("stream_open_resource: unable to measure length.");
                    fclose(fl);
                    return FALSE;
                }
                dataresources[ix].ptr = malloc(dataresources[ix].len+1);
                fseek(fl, 0, SEEK_SET);
                int got = fread(dataresources[ix].ptr, 1, dataresources[ix].len, fl);
                fclose(fl);
                if (got != dataresources[ix].len) {
                    gli_strict_warning("stream_open_resource: unable to read all resource data.");
                    return FALSE;
                }
            }
            *ptr = dataresources[ix].ptr;
            *len = dataresources[ix].len;
            return TRUE;
        }
    }

    return FALSE;
}

/* This opens a file for reading or writing. (You cannot open a file
   for appending using this call.)

   This should be used only by glkunix_startup_code() and the
   glkunix_autosave system.
*/
strid_t glkunix_stream_open_pathname_gen(char *pathname, glui32 writemode,
    glui32 textmode, glui32 rock)
{
    return gli_stream_open_pathname(pathname, (writemode != 0), (textmode != 0), rock);
}

/* This opens a file for reading. It is a less-general form of 
   glkunix_stream_open_pathname_gen(), preserved for backwards 
   compatibility.

   This should be used only by glkunix_startup_code().
*/
strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, 
    glui32 rock)
{
    if (!inittime)
        return 0;
    return gli_stream_open_pathname(pathname, FALSE, (textmode != 0), rock);
}
