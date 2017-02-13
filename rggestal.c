/* gtgestal.c: The Gestalt system
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glk.h"
#include "remglk.h"

glui32 glk_gestalt(glui32 id, glui32 val)
{
    return glk_gestalt_ext(id, val, NULL, 0);
}

glui32 glk_gestalt_ext(glui32 id, glui32 val, glui32 *arr, glui32 arrlen)
{
    switch (id) {
        
        case gestalt_Version:
            /* This implements Glk spec version 0.7.5. */
            return 0x00000705;
        
        case gestalt_LineInput:
            if (val >= 32 && val < 127)
                return TRUE;
            else
                return FALSE;
                
        case gestalt_CharInput: 
            if (val >= 32 && val < 127)
                return TRUE;
            else if (val == keycode_Return)
                return TRUE;
            else {
                /* We're doing UTF-8 input, so we can input any Unicode
                   character. Except control characters. */
                return (val >= 160 && val < 0x200000);
            }
        
        case gestalt_CharOutput: 
            if (val >= 32 && val < 127) {
                if (arr && arrlen >= 1)
                    arr[0] = 1;
                return gestalt_CharOutput_ExactPrint;
            }
            else {
                /* cheaply, we don't do any translation of printed
                    characters, so the output is always one character 
                    even if it's wrong. */
                if (arr && arrlen >= 1)
                    arr[0] = 1;
                /* We're doing UTF-8 output, so we can print any Unicode
                   character. Except control characters. */
                if (val >= 160 && val < 0x200000)
                    return gestalt_CharOutput_ExactPrint;
                else
                    return gestalt_CharOutput_CannotPrint;
            }
            
        case gestalt_MouseInput: 
            return FALSE;
            
        case gestalt_Timer: 
            return pref_timersupport;

        case gestalt_Graphics:
        case gestalt_GraphicsTransparency:
            return pref_graphicssupport;

        case gestalt_GraphicsCharInput:
            return FALSE;
            
        case gestalt_DrawImage:
            if (pref_graphicssupport) {
                if (val == wintype_TextBuffer)
                    return TRUE;
                if (val == wintype_Graphics && pref_graphicswinsupport)
                    return TRUE;
            }
            return FALSE;
            
        case gestalt_Unicode:
            return TRUE;
            
        case gestalt_UnicodeNorm:
            return TRUE;
            
        case gestalt_Sound:
        case gestalt_SoundVolume:
        case gestalt_SoundNotify: 
        case gestalt_SoundMusic:
            return FALSE;

        case gestalt_Hyperlinks: 
        case gestalt_HyperlinkInput:
            return pref_hyperlinksupport;
 
        case gestalt_LineInputEcho:
            return TRUE;

        case gestalt_LineTerminators:
            return FALSE; /* ### for now */
        case gestalt_LineTerminatorKey:
            /* RemGlk never uses the escape or function keys for anything,
               so we'll allow them to be line terminators. */
            if (val == keycode_Escape)
                return TRUE;
            if (val >= keycode_Func12 && val <= keycode_Func1)
                return TRUE;
            return FALSE;

        case gestalt_DateTime:
            return TRUE;

        case gestalt_ResourceStream:
            return TRUE;

        default:
            return 0;

    }
}

