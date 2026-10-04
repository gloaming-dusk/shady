#ifndef SHADY_IPC_JSON_H
#define SHADY_IPC_JSON_H

/*
 * Just enough JSON for the IPC socket: a growable writer, and an in-place
 * parser for one flat request object per line. Values may be strings,
 * numbers, booleans, null or arrays of strings; nested objects are rejected.
 * Shared by the compositor (src/ipc/ipc.c) and shadyctl.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

struct json_buf {
	char *data;
	size_t len;
	size_t cap;
	bool failed; /* allocation failed; contents are incomplete */
};

void json_buf_free(struct json_buf *buf);
void json_buf_clear(struct json_buf *buf);
void json_append(struct json_buf *buf, const char *text, size_t len);
void json_puts(struct json_buf *buf, const char *text);
void json_printf(struct json_buf *buf, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
/* A quoted, escaped JSON string; NULL is written as "". */
void json_string(struct json_buf *buf, const char *text);
/* Remove `count` bytes from the front (consumed output). */
void json_consume(struct json_buf *buf, size_t count);

#define JSON_MAX_FIELDS 16
#define JSON_MAX_ITEMS 32

enum json_type {
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
};

struct json_value {
	enum json_type type;
	bool boolean;
	double number;
	const char *string; /* points into the parsed line */
	const char *items[JSON_MAX_ITEMS]; /* JSON_ARRAY of strings */
	size_t count;
	/* Original text of a number or string, for echoing request ids. */
	const char *raw;
	size_t raw_len;
};

struct json_field {
	const char *key;
	struct json_value value;
};

struct json_object {
	struct json_field fields[JSON_MAX_FIELDS];
	size_t count;
};

/* Parse one object from `line`, decoding strings in place (the line is
 * modified). Returns false and sets *error on malformed input. */
bool json_parse_object(char *line, struct json_object *out, const char **error);
const struct json_value *json_get(const struct json_object *object, const char *key);
const char *json_get_string(const struct json_object *object, const char *key);

#endif
