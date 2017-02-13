/* main.c: Top-level source file
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Glk API which this implements: version 0.7.5.
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
int pref_printversion = FALSE;
int pref_stderr = FALSE;
int pref_fixedmetrics = FALSE;
int pref_screenwidth = 80;
int pref_screenheight = 50;
int pref_timersupport = FALSE;
int pref_hyperlinksupport = FALSE;
int pref_graphicssupport = FALSE;
int pref_graphicswinsupport = FALSE;
char *pref_resourceurl = NULL;
#if GIDEBUG_LIBRARY_SUPPORT
int gli_debugger = FALSE;
#endif /* GIDEBUG_LIBRARY_SUPPORT */

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
        else if (extract_value(argc, argv, "width", ex_Int, &ix, &val, 80))
            pref_screenwidth = val;
        else if (extract_value(argc, argv, "w", ex_Int, &ix, &val, 80))
            pref_screenwidth = val;
        else if (extract_value(argc, argv, "height", ex_Int, &ix, &val, 50))
            pref_screenheight = val;
        else if (extract_value(argc, argv, "h", ex_Int, &ix, &val, 50))
            pref_screenheight = val;
        else if (extract_value(argc, argv, "stderr", ex_Bool, &ix, &val, FALSE))
            pref_stderr = val;
        else if (extract_value(argc, argv, "support", ex_Str, &ix, &val, FALSE)) {
            if (!strcmp(extracted_string, "timer") || !strcmp(extracted_string, "timers"))
                pref_timersupport = TRUE;
            else if (!strcmp(extracted_string, "hyperlink") || !strcmp(extracted_string, "hyperlinks"))
                pref_hyperlinksupport = TRUE;
            else if (!strcmp(extracted_string, "graphics"))
                pref_graphicssupport = TRUE;
            else if (!strcmp(extracted_string, "graphicswin"))
                pref_graphicswinsupport = TRUE;
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
        printf("  -width NUM: manual screen width (default 80)\n");
        printf("  -height NUM: manual screen height (default 50)\n");
        printf("  -support [timer, hyperlinks, graphics, graphicswin]: declare support for various input features\n");
        printf("  -resourceurl STR: URL base for image/sound files\n");
        printf("  -resourcedir STR: path to image/sound files (used to create file: URLs)\n");
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

    gli_initialize_datainput();

    data_metrics_t *metrics = data_metrics_alloc(pref_screenwidth, pref_screenheight);
    if (!pref_fixedmetrics) {
        data_event_t *data = data_event_read();
        if (data->dtag != dtag_Init)
            gli_fatal_error("First input event must be 'init'");
        if (data->supportcaps) {
            /* Set the suppport preference flags. (Bit of a layering 
               violation, but the flags are simple.) */
            if (data->supportcaps->timer)
                pref_timersupport = TRUE;
            if (data->supportcaps->hyperlinks)
                pref_hyperlinksupport = TRUE;
            if (data->supportcaps->graphics)
                pref_graphicssupport = TRUE;
            if (data->supportcaps->graphicswin)
                pref_graphicswinsupport = TRUE;
        }
        /* Copy the metrics into the permanent structure */
        *metrics = *data->metrics;
        data_event_free(data);
    }
    
    /* Initialize things. */
    gli_initialize_misc();
    gli_initialize_windows(metrics);
    gli_initialize_events();

    data_metrics_free(metrics);

    inittime = TRUE;
    if (!glkunix_startup_code(&startdata)) {
        glk_exit();
    }
    inittime = FALSE;

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
    char *cx, *origcx, firstch;
    
    optlen = strlen(optname);
    origcx = argv[*argnum];
    cx = origcx;
    
    firstch = *cx;
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

/* This opens a file for reading or writing. (You cannot open a file
   for appending using this call.)

   This should be used only by glkunix_startup_code(). 
*/
strid_t glkunix_stream_open_pathname_gen(char *pathname, glui32 writemode,
    glui32 textmode, glui32 rock)
{
    if (!inittime)
        return 0;
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
