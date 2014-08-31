#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "get-comics.h"
#include "my-parser.h"

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
	J_C1,
	J_C2,
	J_C3
};

struct JSON_parser_struct {
	JSON_parser_callback callback;
	enum states state;

	/* JSON_T_STRING only */
	char string[256], *str;
	int type;
	int next_state;

	/* JSON_T_INTEGER only */
	int integer;

	/* sanity checking */
	int in_array;
	int in_object;
};

void init_JSON_config(JSON_config* config)
{
    if (config)
	memset(config, 0, sizeof(*config));
}

JSON_parser new_JSON_parser(JSON_config const * config)
{
	if (config->callback == NULL)
		return NULL;

	JSON_parser jc = (JSON_parser)calloc(1, sizeof(*jc));
	if (jc)
		jc->callback = config->callback;
	return jc;
}

static void new_state(JSON_parser jc, enum states new)
{
	if (verbose > 2)
		printf("state %d -> %d\n", jc->state, new);
	jc->state = new;
}

#define LEAVE_OBJECT(new) do {					\
		if (jc->in_object == 0) goto failed;		\
		--jc->in_object;					\
		jc->callback(NULL, JSON_T_OBJECT_END, NULL);	\
		new_state(jc, new);				\
	} while (0)

#define STRING_OBJ(typein, next_statein) do {	\
		new_state(jc, J_STRING);		\
		jc->str = jc->string;		\
		jc->type = typein;		\
		jc->next_state = next_statein;	\
	} while (0)

int JSON_parser_char(JSON_parser jc, int next_char)
{
	JSON_value value;

	switch (jc->state) {
	case J_START:
		if (next_char == '{') {
			++jc->in_object;
			jc->callback(NULL, JSON_T_OBJECT_BEGIN, NULL);
			new_state(jc, J_KEY_START);
		} else if (jc->in_array && next_char == ']') {
			jc->in_array  = 0;
			jc->callback(NULL, JSON_T_ARRAY_END, NULL);
		} else if (next_char == '}')
			LEAVE_OBJECT(J_DONE);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_KEY_START:
		if (next_char == '"')
			STRING_OBJ(JSON_T_KEY, J_COLON);
		else if (next_char == '/')
			new_state(jc, J_C1);
		else if (next_char == '}') /* empty object allowed */
			LEAVE_OBJECT(J_START);
		else if (!isspace(next_char))
			goto failed;
		break;

	case J_STRING:
		if (next_char == '"') {
			*jc->str = '\0';
			value.vu.str.value = jc->string;
			jc->callback(NULL, jc->type, &value);
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
			jc->callback(NULL, JSON_T_INTEGER, &value);
			new_state(jc, J_END);
			return JSON_parser_char(jc, next_char);
		}
		break;

	case J_C1:
		if (next_char == '*')
			new_state(jc, J_C2);
		else
			goto failed;
		break;

	case J_C2:
		if (next_char == '*')
			new_state(jc, J_C3);
		break;

	case J_C3:
		if (next_char == '/')
			new_state(jc, J_KEY_START);
		else
			new_state(jc, J_C2);
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
			jc->callback(NULL, JSON_T_ARRAY_BEGIN, NULL);
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

int JSON_parser_done(JSON_parser jc)
{
	return jc->state == J_DONE;
}

void delete_JSON_parser(JSON_parser jc)
{
    if (jc)
	free(jc);
}
