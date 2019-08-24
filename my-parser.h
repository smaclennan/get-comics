#ifndef my_parser_h
#define my_parser_h

typedef struct JSON_value_struct {
	char *key; // may be NULL
	char *str; // may be NULL
} JSON_value;

typedef int (*JSON_parser_callback)(void* ctx, int type, struct JSON_value_struct* value);

typedef enum
{
    JSON_T_NONE = 0,
    JSON_T_OBJECT_BEGIN,	// optional key
    JSON_T_OBJECT_END,
    JSON_T_ARRAY_BEGIN,		// optional key
    JSON_T_ARRAY_END,
    JSON_T_STRING,			// key and str
} JSON_type;

/* get-comics extension */
int JSON_parse_file(const char *fname, JSON_parser_callback callback, void *callback_ctx);

/* helper function */
int JSON_int(const char *str);

#endif
