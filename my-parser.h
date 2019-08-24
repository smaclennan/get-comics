#ifndef my_parser_h
#define my_parser_h

typedef struct JSON_value_struct {
    union {
	struct {
	    const char *value;
	} str;
    } vu;
} JSON_value;

typedef int (*JSON_parser_callback)(void* ctx, int type, const struct JSON_value_struct* value);

typedef enum
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_STRING,
    JSON_T_KEY
} JSON_type;

/* get-comics extension */
int JSON_parse_file(const char *fname, JSON_parser_callback callback, void *callback_ctx);

/* helper function */
int JSON_int(const char *str);

#endif
