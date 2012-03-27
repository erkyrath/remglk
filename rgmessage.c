/* gtmessag.c: The message line at the bottom of the screen
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"

/* Nothing fancy here. We store a string, and print it on the bottom line.
    If pref_messageline is FALSE, none of these functions do anything. */

static char *msgbuf = NULL;
static int msgbuflen = 0;
static int msgbuf_size = 0;

void gli_msgline_warning(char *msg)
{
    char buf[256];
    
    if (!pref_messageline)
        return;
        
    sprintf(buf, "Glk library error: %s", msg);
    gli_msgline(buf);
}

void gli_msgline_error(char *msg)
{
    fprintf(stderr, "%s\n", msg); /*###*/

    exit(1); /* ### or something */
}

/* ### get rid of this */
void gli_msgline(char *msg)
{
    int len;
    
    if (!pref_messageline)
        return;
        
    if (!msg) 
        msg = "";
    
    len = strlen(msg);
    if (!msgbuf) {
        msgbuf_size = len+80;
        msgbuf = (char *)malloc(msgbuf_size * sizeof(char));
    }
    else if (len+1 > msgbuf_size) {
        while (len+1 > msgbuf_size)
            msgbuf_size *= 2;
        msgbuf = (char *)realloc(msgbuf, msgbuf_size * sizeof(char));
    }

    if (!msgbuf)
        return;
    
    strcpy(msgbuf, msg);
    msgbuflen = len;
    
}

