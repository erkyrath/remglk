typedef enum DTag_enum {
    dtag_Unknown = 0,
    dtag_Init = 1,
    dtag_Refresh = 2,
    dtag_Line = 2,
    dtag_Char = 3,
    dtag_Arrange = 4,
    dtag_Hyperlink = 5,
} DTag;

typedef struct gen_list_struct {
    void **list;
    int count;
    int allocsize;
} gen_list_t;

typedef struct data_event_struct data_event_t;
typedef struct data_update_struct data_update_t;
typedef struct data_window_struct data_window_t;
typedef struct data_input_struct data_input_t;
typedef struct data_line_struct data_line_t;
typedef struct data_span_struct data_span_t;

struct data_metrics_struct {
    glui32 width, height;
    glui32 outspacingx, outspacingy;
    glui32 inspacingx, inspacingy;
    glui32 gridcharwidth, gridcharheight;
    glui32 gridmarginx, gridmarginy;
    glui32 buffercharwidth, buffercharheight;
    glui32 buffermarginx, buffermarginy;
};

struct data_event_struct {
    DTag dtag;
    glsi32 gen;
    glui32 window;
    glui32 charvalue;
    glui32 *linevalue;
    glui32 linelen;
    glui32 terminator;
    data_metrics_t *metrics;
};

struct data_update_struct {
    glsi32 gen;
    int usewindows;
    gen_list_t windows; /* data_window_t */
    gen_list_t contents; /* data_content_t */
    int useinputs;
    gen_list_t inputs; /* data_event_t */
    int disable;
};

struct data_window_struct {
    glui32 window;
    glui32 type;
    glui32 rock;
    grect_t size;
    glui32 gridwidth, gridheight;
};

struct data_input_struct {
    glui32 window;
    glsi32 evtype;
    glsi32 gen;
    glui32 *initstr;
    glui32 initlen;
    glui32 maxlen;
};

struct data_content_struct {
    glui32 window;
    glui32 type;
    gen_list_t lines; /* data_line_t */
    int clear;
};

struct data_line_struct {
    glui32 linenum;
    int append;
    data_span_t *spans;
    int count;
    int allocsize;
};

struct data_span_struct {
    short style;
    glui32 *str; /* This will always be a reference to existing data.
                    Do not free. */
    long len;
};

extern void gli_initialize_datainput(void);

extern void print_ustring_json(glui32 *buf, glui32 len, FILE *fl);
extern void print_string_json(char *buf, FILE *fl);

extern void gen_list_init(gen_list_t *list);
extern void gen_list_free(gen_list_t *list);
extern void gen_list_append(gen_list_t *list, void *val);

extern data_metrics_t *data_metrics_alloc(int width, int height);
extern void data_metrics_free(data_metrics_t *metrics);
extern void data_metrics_print(data_metrics_t *metrics);

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
extern void data_line_add_span(data_line_t *data, short style, glui32 *str, long len);
extern void data_line_print(data_line_t *data, glui32 wintype);
