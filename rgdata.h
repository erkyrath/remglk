typedef enum DTag_enum {
    dtag_Unknown = 0,
    dtag_Init = 1,
    dtag_Refresh = 2,
    dtag_Line = 2,
    dtag_Char = 3,
    dtag_Arrange = 4,
    dtag_Hyperlink = 5,
} DTag;

typedef struct data_input_struct data_input_t;
typedef struct data_metrics_struct data_metrics_t;

struct data_metrics_struct {
    glui32 width, height;
    glui32 outspacingx, outspacingy;
    glui32 inspacingx, inspacingy;
    glui32 gridcharwidth, gridcharheight;
    glui32 gridmarginx, gridmarginy;
    glui32 buffercharwidth, buffercharheight;
    glui32 buffermarginx, buffermarginy;
};

struct data_input_struct {
    DTag dtag;
    glsi32 gen;
    glui32 window;
    glui32 charvalue;
    glui32 *linevalue;
    glui32 linelen;
    glui32 terminator;
    data_metrics_t *metrics;
};

extern void gli_initialize_datainput(void);
extern data_input_t *data_input_read(void);
extern void data_input_print(data_input_t *data);

