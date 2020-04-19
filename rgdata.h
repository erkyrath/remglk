/* rgdata.h: JSON data structure header
        for RemGlk, remote-procedure-call implementation of the Glk API.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/
*/

/* There are two levels of data structures here. The high-level ones 
   (data_event_t, data_update_t, data_window_t, etc) are built and accepted
   by the other parts of the library.

   The low-level structure, data_raw_t, is defined and used only inside
   rgdata.c. It maps directly to and from JSON objects.

   Every data structure has a print() method, which sends it to stdout
   as a JSON structure.
 */

typedef enum DTag_enum {
    dtag_Unknown = 0,
    dtag_Init = 1,
    dtag_Refresh = 2,
    dtag_Line = 3,
    dtag_Char = 4,
    dtag_Arrange = 5,
    dtag_Redraw = 6,
    dtag_Hyperlink = 7,
    dtag_Timer = 8,
    dtag_SpecialResponse = 9,
    dtag_DebugInput = 10,
} DTag;

/* gen_list_t: A boring little structure which holds a dynamic list of
   void pointers. Several of the high-level data objects use these
   to store lists of other high-level data objects. (You embed a
   gen_list_t directly, rather than a pointer to it.) */
typedef struct gen_list_struct {
    void **list;
    int count;
    int allocsize;
} gen_list_t;

typedef struct data_event_struct data_event_t;
typedef struct data_supportcaps_struct data_supportcaps_t;
typedef struct data_update_struct data_update_t;
typedef struct data_window_struct data_window_t;
typedef struct data_input_struct data_input_t;
typedef struct data_line_struct data_line_t;
typedef struct data_span_struct data_span_t;
typedef struct data_specialspan_struct data_specialspan_t;

/* data_metrics_t: Defines the display metrics. */
struct data_metrics_struct {
    glui32 width, height;
    glui32 outspacingx, outspacingy;
    glui32 inspacingx, inspacingy;
    double gridcharwidth, gridcharheight;
    glui32 gridmarginx, gridmarginy;
    double buffercharwidth, buffercharheight;
    glui32 buffermarginx, buffermarginy;
    glui32 graphicsmarginx, graphicsmarginy;
};

/* data_supportcaps_t: List of I/O capabilities of the client. */
struct data_supportcaps_struct {
    int timer;
    int hyperlinks;
    int graphics;
    int graphicswin;
    int sound;
};

/* data_event_t: Represents an input event (either the initial setup event,
   or user input). */
struct data_event_struct {
    DTag dtag;
    glsi32 gen;
    glui32 window;
    glui32 charvalue;
    glui32 *linevalue;
    glui32 linelen;
    glui32 terminator;
    glui32 linkvalue;
    data_metrics_t *metrics;
    data_supportcaps_t *supportcaps;
};

/* data_update_t: Represents a complete output update, including what
   happened to all the windows this cycle. */
struct data_update_struct {
    glsi32 gen;
    int usewindows;
    gen_list_t windows; /* data_window_t */
    gen_list_t contents; /* data_content_t */
    int useinputs;
    gen_list_t inputs; /* data_event_t */
    int includetimer;
    glui32 timer;
    data_specialreq_t *specialreq;
    gen_list_t debuglines; /* char* (null-terminated UTF8) */
    int disable;
};

/* data_window_t: Represents one window, either newly created, resized, or
   repositioned. */
struct data_window_struct {
    glui32 window;
    glui32 type;
    glui32 rock;
    grect_t size;
    glui32 gridwidth, gridheight;
};

/* data_input_t: Represents the input request of one window. */
struct data_input_struct {
    glui32 window;
    glsi32 evtype;
    glsi32 gen;
    glui32 *initstr;
    glui32 initlen;
    glui32 maxlen;
    int cursorpos; /* only for grids */
    glsi32 xpos, ypos; /* only if cursorpos */
    int hyperlink;
};

/* data_content_t: Represents the output changes of one window (text
   updates). Also used for graphics window updates, because that was
   easiest. */
struct data_content_struct {
    glui32 window;
    glui32 type; /* window type */
    gen_list_t lines; /* data_line_t */
    int clear;
};

/* data_line_t: One line of text in a data_content_t. This is used for
   both grid windows and buffer windows. (In a buffer window, a "line"
   is a complete paragraph.) */
struct data_line_struct {
    glui32 linenum;
    int append;
    int flowbreak;
    data_span_t *spans;
    int count;
    int allocsize;
};

/* data_span_t: One style-span of text in a data_line_t. */
struct data_span_struct {
    short style;
    glui32 hyperlink;
    glui32 *str; /* This will always be a reference to existing data.
                    Do not free. */
    long len;
    data_specialspan_t *special; /* Do not free. */
};

typedef enum SpecialType_enum {
    specialtype_None = 0,
    specialtype_FlowBreak = 1,
    specialtype_Image = 2,
    specialtype_SetColor = 3,
    specialtype_Fill = 4,
} SpecialType;

/* data_specialspan_t: Extra things that a data_span_t can represent.
   Not all these fields are used for all types. */
struct data_specialspan_struct {
    SpecialType type;
    glui32 image; /* (Image) */
    glui32 chunktype; /* (Image) JPEG or PNG */
    int hasdimensions; /* (Fill) */
    glui32 xpos; /* (Fill, Image in graphicswin) */
    glui32 ypos; /* (Fill, Image in graphicswin) */
    glui32 width; /* (Fill, Image) */
    glui32 height /* (Fill, Image) */;
    glui32 alignment; /* (Image in bufferwin) */
    glui32 hyperlink; /* (Image in bufferwin) */
    char *alttext; /* (Image) Reference to existing data. */
    int hascolor; /* (SetColor, Fill) */
    glui32 color; /* (SetColor, Fill) */
};

/* data_specialreq_t: A special input request. */
struct data_specialreq_struct {
    glui32 filemode;
    glui32 filetype;
    char *gameid; /* may be null */
};

extern void gli_initialize_datainput(void);

extern void print_ustring_json(glui32 *buf, glui32 len, FILE *fl);
extern void print_utf8string_json(char *buf, FILE *fl);
extern void print_string_json(char *buf, FILE *fl);

extern void gen_list_init(gen_list_t *list);
extern void gen_list_free(gen_list_t *list);
extern void gen_list_append(gen_list_t *list, void *val);

extern data_metrics_t *data_metrics_alloc(int width, int height);
extern void data_metrics_free(data_metrics_t *metrics);
extern void data_metrics_print(data_metrics_t *metrics);

extern data_supportcaps_t *data_supportcaps_alloc(void);
extern void data_supportcaps_free(data_supportcaps_t *supportcaps);
extern void data_supportcaps_print(data_supportcaps_t *supportcaps);

extern data_event_t *data_event_read(void);
extern void data_event_free(data_event_t *data);
extern void data_event_print(data_event_t *data);

extern data_update_t *data_update_alloc(void);
extern void data_update_free(data_update_t *data);
extern void data_update_print(data_update_t *data);

extern data_window_t *data_window_alloc(glui32 window, glui32 type, glui32 rock);
extern void data_window_free(data_window_t *data);
extern void data_window_print(data_window_t *data);

extern data_input_t *data_input_alloc(glui32 window, glui32 evtype);
extern void data_input_free(data_input_t *data);
extern void data_input_print(data_input_t *data);

extern data_content_t *data_content_alloc(glui32 window, glui32 type);
extern void data_content_free(data_content_t *data);
extern void data_content_print(data_content_t *data);

extern data_line_t *data_line_alloc(void);
extern void data_line_free(data_line_t *data);
extern void data_line_add_span(data_line_t *data, short style, glui32 hyperlink, glui32 *str, long len);
extern void data_line_add_specialspan(data_line_t *data, data_specialspan_t *special);
extern void data_line_print(data_line_t *data, glui32 wintype);

extern data_specialspan_t *data_specialspan_alloc(SpecialType type);
extern void data_specialspan_free(data_specialspan_t *data);
extern void data_specialspan_print(data_specialspan_t *dat, glui32 wintype);

extern data_specialreq_t *data_specialreq_alloc(glui32 filemode, glui32 filetype);
extern void data_specialreq_free(data_specialreq_t *data);
extern void data_specialreq_print(data_specialreq_t *data);

