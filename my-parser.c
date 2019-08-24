#include "get-comics.h"
#include "my-parser.h"

// #define DEBUG_CB

enum states {
	J_START,
	J_KEY_START,
	J_STRING,
	J_STRING_QUOTE,
	J_INT,
	J_COLON,
	J_OBJ,
	J_END,
	J_DONE,
	J_FAILED,
	J_COMMENT,
};

typedef struct JSON_parser_struct {
	FILE *fp;

	JSON_parser_callback callback;
	void *callback_ctx;

	enum states state;

	/* JSON_T_STRING only */
	char string[256], *str;
	int type;
	int next_state;

	/* JSON_T_INTEGER only */
	int integer;

	/* J_COMMENT only */
	enum states start_state;
	int nested;

	/* sanity checking */
	int in_array;
	int in_object;
} *JSON_parser;

static struct JSON_parser_struct JC;

static inline int get_char(JSON_parser jc)
{
	return fgetc(jc->fp);
}

/* Only one level of peek */
static inline int peek_char(JSON_parser jc)
{
	int ch = fgetc(jc->fp);
	ungetc(ch, jc->fp);
	return ch;
}

static void new_state(JSON_parser jc, enum states new)
{
	if (verbose > 3)
		printf("state %d -> %d\n", jc->state, new);
	jc->state = new;
}

#define LEAVE_OBJECT(new) do {					\
		if (jc->in_object == 0) goto failed;		\
		--jc->in_object;					\
		jc->callback(jc->callback_ctx, JSON_T_OBJECT_END, NULL);	\
		new_state(jc, new);				\
	} while (0)

#define STRING_OBJ(typein, next_statein) do {	\
		new_state(jc, J_STRING);		\
		jc->str = jc->string;		\
		jc->type = typein;		\
		jc->next_state = next_statein;	\
	} while (0)

static void start_comment(JSON_parser jc)
{
	get_char(jc);
	jc->start_state = jc->state;
	new_state(jc, J_COMMENT);
	jc->nested = 1;
}

static int JSON_parser_char(JSON_parser jc, int next_char)
{
	JSON_value value;

	switch (jc->state) {
	case J_START:
		if (next_char == '{') {
			++jc->in_object;
			jc->callback(jc->callback_ctx, JSON_T_OBJECT_BEGIN, NULL);
			new_state(jc, J_KEY_START);
		} else if (jc->in_array && next_char == ']') {
			jc->in_array  = 0;
			jc->callback(jc->callback_ctx, JSON_T_ARRAY_END, NULL);
		} else if (next_char == '}')
			LEAVE_OBJECT(J_DONE);
		else if (next_char == '/' && peek_char(jc) == '*')
			start_comment(jc);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_KEY_START:
		if (next_char == '"')
			STRING_OBJ(JSON_T_KEY, J_COLON);
		else if (next_char == '/' && peek_char(jc) == '*')
			start_comment(jc);
		else if (next_char == '}') /* empty object allowed */
			LEAVE_OBJECT(J_START);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_STRING:
		if (next_char == '"') {
			*jc->str = '\0';
			value.vu.str.value = jc->string;
			jc->callback(jc->callback_ctx, jc->type, &value);
			new_state(jc, jc->next_state);
		} else if (next_char == '\\')
			new_state(jc, J_STRING_QUOTE);
		else
			*jc->str++ = next_char;
		break;

	case J_STRING_QUOTE:
		*jc->str++ = next_char;
		new_state(jc, J_STRING);
		break;

	case J_INT:
		if (isdigit(next_char))
			jc->integer = jc->integer * 10 + next_char - '0';
		else {
			value.vu.integer_value = jc->integer;
			jc->callback(jc->callback_ctx, JSON_T_INTEGER, &value);
			new_state(jc, J_END);
			return JSON_parser_char(jc, next_char);
		}
		break;

	case J_COMMENT:
		if (next_char == '*' && peek_char(jc) == '/') {
			get_char(jc);
			if (--jc->nested <= 0)
				new_state(jc, jc->start_state);
		} else if (next_char == '/' && peek_char(jc) == '*') {
			get_char(jc);
			jc->nested++;
		}
		break;

	case J_COLON:
		if (next_char == ':')
			new_state(jc, J_OBJ);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_OBJ:
		if (isspace(next_char))
			break;
		switch (next_char) {
		case '"':
			STRING_OBJ(JSON_T_STRING, J_END);
			break;
		case '[':
			jc->callback(jc->callback_ctx, JSON_T_ARRAY_BEGIN, NULL);
			jc->in_array = 1;
			new_state(jc, J_START);
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			jc->integer = next_char - '0';
			new_state(jc, J_INT);
			break;
		default:
			goto failed;
		}
		break;

	case J_END:
		if (next_char == '}')
			LEAVE_OBJECT(J_DONE);
		else if (next_char == ',')
			new_state(jc, J_KEY_START);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_DONE:
		if (jc->in_array && next_char == ',')
			new_state(jc, J_START);
		else if (!isspace(next_char)) {
			new_state(jc, J_FAILED);
			goto failed;
		}
		break;
	case J_FAILED:
		break;
	}

	return 1;

failed:
	printf("Bad char '%c' in state %d\n", next_char, jc->state);
	new_state(jc, J_FAILED);
	return 0;
}

#ifdef DEBUG_CB
static JSON_parser_callback real_cb;

static int debug_cb(void* ctx, int type, const struct JSON_value_struct* value)
{
	switch (type) {
	case JSON_T_ARRAY_BEGIN:
		fputs("Array begin\n", stderr);
		break;
    case JSON_T_ARRAY_END:
		fputs("Array_end\n", stderr);
		break;
    case JSON_T_OBJECT_BEGIN:
		fputs("Obj begin\n", stderr);
		break;
    case JSON_T_OBJECT_END:
		fputs("Obj end\n", stderr);
		break;
    case JSON_T_INTEGER:
		fprintf(stderr, "Int %d\n", value->vu.integer_value);
		break;
    case JSON_T_STRING:
		fprintf(stderr, "Str '%s'\n", value->vu.str.value);
		break;
    case JSON_T_KEY:
		fprintf(stderr, "Key '%s'\n", value->vu.str.value);
		break;
	}

	return real_cb(ctx, type, value);
}
#endif


int JSON_parse_file(const char *fname, JSON_parser_callback callback, void *callback_ctx)
{
	if (!callback)
		return 2;

	memset(&JC, 0, sizeof(JC));
	JC.callback = callback;
	JC.callback_ctx = callback_ctx;

#ifdef DEBUG_CB
	real_cb = callback;
	JC.callback = debug_cb;
#endif

	JC.fp = fopen(fname, "r");
	if (!JC.fp) {
		my_perror((char *)fname);
		return -1;
	}

	int count = 0, next_char;
	while ((next_char = get_char(&JC)) > 0) {
		++count;
		if (!JSON_parser_char(&JC, next_char)) {
			printf("JSON_parser: syntax error byte %d\n", count);
			JC.state = 0; /* fail */
			break;
		}
	}

	fclose(JC.fp);

	int rc = JC.state == J_DONE ? 0 : 1;
	if (rc)
		printf("JSON_parser: parse failed\n");

	return rc;
}
