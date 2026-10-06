/* The IPC JSON helpers: escaping round-trips through the parser, and
 * malformed or unsupported requests are rejected with an error. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../compositor/src/ipc/json.h"

static void parse_ok(const char *text, struct json_object *out, char *storage, size_t size) {
	snprintf(storage, size, "%s", text);
	const char *error = NULL;
	if (!json_parse_object(storage, out, &error)) {
		fprintf(stderr, "unexpected parse error for %s: %s\n", text, error);
		assert(0);
	}
}

static void parse_fails(const char *text) {
	char storage[512];
	struct json_object out;
	const char *error = NULL;
	snprintf(storage, sizeof(storage), "%s", text);
	assert(!json_parse_object(storage, &out, &error));
	assert(error && *error);
}

int main(void) {
	char storage[512];
	struct json_object obj;

	parse_ok(" { \"id\" : 7, \"cmd\":\"window.focus\", \"state\": false, \"x\": null,"
		" \"events\": [\"window.focused\", \"workspace.changed\"], \"n\": -1.5e2 } ",
		&obj, storage, sizeof(storage));
	assert(obj.count == 6);
	const struct json_value *id = json_get(&obj, "id");
	assert(id && id->type == JSON_NUMBER && id->number == 7);
	assert(id->raw_len == 1 && id->raw[0] == '7');
	assert(!strcmp(json_get_string(&obj, "cmd"), "window.focus"));
	assert(json_get(&obj, "state")->type == JSON_BOOL && !json_get(&obj, "state")->boolean);
	assert(json_get(&obj, "x")->type == JSON_NULL);
	const struct json_value *events = json_get(&obj, "events");
	assert(events->type == JSON_ARRAY && events->count == 2);
	assert(!strcmp(events->items[1], "workspace.changed"));
	assert(json_get(&obj, "n")->number == -150.0);
	assert(!json_get(&obj, "missing") && !json_get_string(&obj, "id"));

	parse_ok("{}", &obj, storage, sizeof(storage));
	assert(obj.count == 0);

	/* Escapes, including a surrogate pair, decode to UTF-8. */
	parse_ok("{\"t\":\"a\\\"b\\\\c\\n\\u00e9\\ud83d\\ude00\"}", &obj, storage, sizeof(storage));
	assert(!strcmp(json_get_string(&obj, "t"), "a\"b\\c\n\xc3\xa9\xf0\x9f\x98\x80"));

	/* Writer output parses back to the same string. */
	struct json_buf b = {0};
	const char *title = "quote \" slash \\ tab \t bell \a ünï";
	json_puts(&b, "{\"title\":");
	json_string(&b, title);
	json_printf(&b, ",\"n\":%d}", 42);
	assert(!b.failed && strstr(b.data, "\\u0007"));
	parse_ok(b.data, &obj, storage, sizeof(storage));
	assert(!strcmp(json_get_string(&obj, "title"), title));
	json_consume(&b, 2);
	assert(!strncmp(b.data, "title", 5));
	json_buf_free(&b);

	parse_fails("");
	parse_fails("[1,2]");
	parse_fails("{\"a\":1");
	parse_fails("{\"a\":1,}");
	parse_fails("{\"a\" 1}");
	parse_fails("{\"a\":{\"b\":1}}");
	parse_fails("{\"a\":[1]}");
	parse_fails("{\"a\":\"unterminated}");
	parse_fails("{\"a\":\"\\x\"}");
	parse_fails("{\"a\":\"\\ud83d\"}");
	parse_fails("{\"a\":\"\\u0000\"}");
	parse_fails("{\"a\":tru}");
	parse_fails("{\"a\":1} trailing");

	puts("ipc-json: PASS");
	return 0;
}
