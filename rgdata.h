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

struct data_input_struct {
    DTag dtag;
    glsi32 gen;
    glui32 window;
    glui32 value;
    glui32 terminator;
    data_metrics_t *metrics;
};

extern data_input_t *data_input_read(void);

