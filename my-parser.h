#ifndef my_parser_h
#define my_parser_h

typedef struct JSON_value_struct {
    union {
	int integer_value;
	struct {
	    const char *value;
	} str;
    } vu;
} JSON_value;

typedef int (*JSON_parser_callback)(void* ctx, int type, const struct JSON_value_struct* value);

typedef struct {
    JSON_parser_callback    callback;
    int                     depth;
    int                     allow_comments;
} JSON_config;

typedef struct JSON_parser_struct *JSON_parser;

typedef enum
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_INTEGER,
    JSON_T_STRING,
    JSON_T_KEY
} JSON_type;

void init_JSON_config(JSON_config * config);
JSON_parser new_JSON_parser(JSON_config const* config);
void delete_JSON_parser(JSON_parser jc);
int JSON_parser_char(JSON_parser jc, int next_char);
int JSON_parser_done(JSON_parser jc);

#endif

