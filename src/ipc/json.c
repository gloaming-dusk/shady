#include "json.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void json_buf_free(struct json_buf *buf) {
	free(buf->data);
	*buf = (struct json_buf){0};
}

void json_buf_clear(struct json_buf *buf) {
	buf->len = 0;
	buf->failed = false;
	if (buf->data) buf->data[0] = '\0';
}

static bool reserve(struct json_buf *buf, size_t extra) {
	if (buf->failed) return false;
	if (buf->len + extra + 1 <= buf->cap) return true;
	size_t cap = buf->cap ? buf->cap : 256;
	while (cap < buf->len + extra + 1) cap *= 2;
	char *data = realloc(buf->data, cap);
	if (!data) {
		buf->failed = true;
		return false;
	}
	buf->data = data;
	buf->cap = cap;
	return true;
}

void json_append(struct json_buf *buf, const char *text, size_t len) {
	if (!reserve(buf, len)) return;
	memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
}

void json_puts(struct json_buf *buf, const char *text) {
	json_append(buf, text, strlen(text));
}

void json_printf(struct json_buf *buf, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	char small[128];
	int n = vsnprintf(small, sizeof(small), fmt, args);
	va_end(args);
	if (n < 0) return;
	if ((size_t)n < sizeof(small)) {
		json_append(buf, small, (size_t)n);
		return;
	}
	if (!reserve(buf, (size_t)n)) return;
	va_start(args, fmt);
	vsnprintf(buf->data + buf->len, (size_t)n + 1, fmt, args);
	va_end(args);
	buf->len += (size_t)n;
}

void json_string(struct json_buf *buf, const char *text) {
	json_append(buf, "\"", 1);
	for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
		switch (*p) {
		case '"': json_append(buf, "\\\"", 2); break;
		case '\\': json_append(buf, "\\\\", 2); break;
		case '\n': json_append(buf, "\\n", 2); break;
		case '\r': json_append(buf, "\\r", 2); break;
		case '\t': json_append(buf, "\\t", 2); break;
		default:
			if (*p < 0x20) json_printf(buf, "\\u%04x", *p);
			else json_append(buf, (const char *)p, 1);
		}
	}
	json_append(buf, "\"", 1);
}

void json_consume(struct json_buf *buf, size_t count) {
	if (count >= buf->len) {
		json_buf_clear(buf);
		return;
	}
	memmove(buf->data, buf->data + count, buf->len - count);
	buf->len -= count;
	buf->data[buf->len] = '\0';
}

/* ---- parser ----------------------------------------------------------- */

struct parser {
	char *p;
	const char *error;
};

static void skip_ws(struct parser *ps) {
	while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n') ps->p++;
}

static bool fail(struct parser *ps, const char *error) {
	if (!ps->error) ps->error = error;
	return false;
}

static int hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static bool read_hex4(const char *s, uint32_t *out) {
	uint32_t v = 0;
	for (int i = 0; i < 4; i++) {
		int d = hex_digit(s[i]);
		if (d < 0) return false;
		v = v * 16 + (uint32_t)d;
	}
	*out = v;
	return true;
}

/* UTF-8 is never longer than the \uXXXX escape(s) it replaces. */
static char *put_utf8(char *w, uint32_t cp) {
	if (cp < 0x80) {
		*w++ = (char)cp;
	} else if (cp < 0x800) {
		*w++ = (char)(0xc0 | (cp >> 6));
		*w++ = (char)(0x80 | (cp & 0x3f));
	} else if (cp < 0x10000) {
		*w++ = (char)(0xe0 | (cp >> 12));
		*w++ = (char)(0x80 | ((cp >> 6) & 0x3f));
		*w++ = (char)(0x80 | (cp & 0x3f));
	} else {
		*w++ = (char)(0xf0 | (cp >> 18));
		*w++ = (char)(0x80 | ((cp >> 12) & 0x3f));
		*w++ = (char)(0x80 | ((cp >> 6) & 0x3f));
		*w++ = (char)(0x80 | (cp & 0x3f));
	}
	return w;
}

/* Decode a string in place; on success *out is NUL-terminated. */
static bool parse_string(struct parser *ps, const char **out) {
	if (*ps->p != '"') return fail(ps, "expected string");
	char *r = ++ps->p;
	char *w = r;
	*out = w;
	for (;;) {
		unsigned char c = (unsigned char)*r;
		if (c == '\0') return fail(ps, "unterminated string");
		if (c < 0x20) return fail(ps, "control character in string");
		if (c == '"') break;
		if (c != '\\') {
			*w++ = (char)c;
			r++;
			continue;
		}
		r++;
		switch (*r) {
		case '"': *w++ = '"'; r++; break;
		case '\\': *w++ = '\\'; r++; break;
		case '/': *w++ = '/'; r++; break;
		case 'b': *w++ = '\b'; r++; break;
		case 'f': *w++ = '\f'; r++; break;
		case 'n': *w++ = '\n'; r++; break;
		case 'r': *w++ = '\r'; r++; break;
		case 't': *w++ = '\t'; r++; break;
		case 'u': {
			uint32_t cp;
			if (!read_hex4(r + 1, &cp)) return fail(ps, "bad \\u escape");
			r += 5;
			if (cp >= 0xd800 && cp < 0xdc00) {
				uint32_t lo;
				if (r[0] != '\\' || r[1] != 'u' || !read_hex4(r + 2, &lo) ||
						lo < 0xdc00 || lo >= 0xe000)
					return fail(ps, "bad surrogate pair");
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				r += 6;
			} else if (cp >= 0xdc00 && cp < 0xe000) {
				return fail(ps, "bad surrogate pair");
			}
			if (cp == 0) return fail(ps, "NUL in string");
			w = put_utf8(w, cp);
			break;
		}
		default:
			return fail(ps, "bad escape");
		}
	}
	ps->p = r + 1;
	*w = '\0';
	return true;
}

static bool parse_number(struct parser *ps, struct json_value *v) {
	char *end = NULL;
	v->raw = ps->p;
	v->number = strtod(ps->p, &end);
	if (end == ps->p) return fail(ps, "bad value");
	v->raw_len = (size_t)(end - ps->p);
	v->type = JSON_NUMBER;
	ps->p = end;
	return true;
}

static bool literal(struct parser *ps, const char *word) {
	size_t n = strlen(word);
	if (strncmp(ps->p, word, n) != 0) return false;
	ps->p += n;
	return true;
}

static bool parse_value(struct parser *ps, struct json_value *v) {
	skip_ws(ps);
	*v = (struct json_value){0};
	switch (*ps->p) {
	case '"':
		v->type = JSON_STRING;
		return parse_string(ps, &v->string);
	case '[':
		v->type = JSON_ARRAY;
		ps->p++;
		skip_ws(ps);
		if (*ps->p == ']') {
			ps->p++;
			return true;
		}
		for (;;) {
			skip_ws(ps);
			if (v->count == JSON_MAX_ITEMS) return fail(ps, "array too long");
			if (!parse_string(ps, &v->items[v->count]))
				return fail(ps, "arrays may only hold strings");
			v->count++;
			skip_ws(ps);
			if (*ps->p == ']') {
				ps->p++;
				return true;
			}
			if (*ps->p != ',') return fail(ps, "expected ',' or ']'");
			ps->p++;
		}
	case '{':
		return fail(ps, "nested objects are not supported");
	default:
		if (literal(ps, "true")) {
			v->type = JSON_BOOL;
			v->boolean = true;
			return true;
		}
		if (literal(ps, "false")) {
			v->type = JSON_BOOL;
			return true;
		}
		if (literal(ps, "null")) {
			v->type = JSON_NULL;
			return true;
		}
		return parse_number(ps, v);
	}
}

bool json_parse_object(char *line, struct json_object *out, const char **error) {
	struct parser ps = { .p = line };
	out->count = 0;
	skip_ws(&ps);
	if (*ps.p != '{') {
		*error = "expected a JSON object";
		return false;
	}
	ps.p++;
	skip_ws(&ps);
	if (*ps.p == '}') {
		ps.p++;
	} else {
		for (;;) {
			skip_ws(&ps);
			if (out->count == JSON_MAX_FIELDS) {
				fail(&ps, "too many fields");
				break;
			}
			struct json_field *f = &out->fields[out->count];
			if (!parse_string(&ps, &f->key)) break;
			skip_ws(&ps);
			if (*ps.p != ':') {
				fail(&ps, "expected ':'");
				break;
			}
			ps.p++;
			if (!parse_value(&ps, &f->value)) break;
			out->count++;
			skip_ws(&ps);
			if (*ps.p == '}') {
				ps.p++;
				break;
			}
			if (*ps.p != ',') {
				fail(&ps, "expected ',' or '}'");
				break;
			}
			ps.p++;
		}
	}
	if (!ps.error) {
		skip_ws(&ps);
		if (*ps.p) fail(&ps, "trailing characters after object");
	}
	if (ps.error) {
		*error = ps.error;
		return false;
	}
	return true;
}

const struct json_value *json_get(const struct json_object *object, const char *key) {
	for (size_t i = 0; i < object->count; i++)
		if (!strcmp(object->fields[i].key, key)) return &object->fields[i].value;
	return NULL;
}

const char *json_get_string(const struct json_object *object, const char *key) {
	const struct json_value *v = json_get(object, key);
	return v && v->type == JSON_STRING ? v->string : NULL;
}
