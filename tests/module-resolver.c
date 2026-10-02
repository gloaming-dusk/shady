#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wlr/util/log.h>

#include "../src/shady.h"
#include "../src/module/module.h"

static int failures;

static void expect_true(bool value, const char *name) {
	if (!value) {
		fprintf(stderr, "module-resolver: FAIL: %s\n", name);
		failures++;
	}
}

static void expect_false(bool value, const char *name) {
	expect_true(!value, name);
}

static void expect_order(struct shady_server *server,
		const char *first, const char *second, const char *name) {
	ssize_t a = shady_module_index_by_name(server, first);
	ssize_t b = shady_module_index_by_name(server, second);
	if (a < 0 || b < 0 || a >= b) {
		fprintf(stderr,
			"module-resolver: FAIL: %s (expected %s before %s, got %zd/%zd)\n",
			name, first, second, a, b);
		failures++;
	}
}

static bool disabled_by_default(struct shady_server *server) {
	(void)server;
	return false;
}

static void reset_server(struct shady_server *server) {
	memset(server, 0, sizeof(*server));
	shady_modules_init(&server->modules);
	shady_events_init(server);
}

static void finish_server(struct shady_server *server) {
	shady_events_finish(server);
}

static void test_dependency_order(void) {
	static const char *const provides_core[] = {"cap.core", NULL};
	static const char *const requires_core[] = {"cap.core", NULL};
	static const struct shady_module consumer = {
		.name = "consumer",
		.requires = requires_core,
	};
	static const struct shady_module provider = {
		.name = "provider",
		.provides = provides_core,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &consumer),
		"register consumer");
	expect_true(shady_modules_register(&server.modules, &provider),
		"register provider");
	expect_true(shady_modules_resolve(&server), "resolve dependency order");
	expect_order(&server, "provider", "consumer", "provider precedes consumer");
	finish_server(&server);
}

static void test_missing_required(void) {
	static const char *const requires_missing[] = {"cap.missing", NULL};
	static const struct shady_module consumer = {
		.name = "missing-consumer",
		.requires = requires_missing,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &consumer),
		"register missing consumer");
	expect_false(shady_modules_resolve(&server), "reject missing required capability");
	finish_server(&server);
}

static void test_optional_missing(void) {
	static const char *const optional_missing[] = {"cap.optional", NULL};
	static const struct shady_module module = {
		.name = "optional-consumer",
		.optional_requires = optional_missing,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &module),
		"register optional consumer");
	expect_true(shady_modules_resolve(&server), "allow missing optional capability");
	finish_server(&server);
}

static void test_duplicate_provider(void) {
	static const char *const provides_same[] = {"cap.same", NULL};
	static const struct shady_module a = {
		.name = "provider-a",
		.provides = provides_same,
	};
	static const struct shady_module b = {
		.name = "provider-b",
		.provides = provides_same,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &a), "register provider a");
	expect_true(shady_modules_register(&server.modules, &b), "register provider b");
	expect_false(shady_modules_resolve(&server), "reject duplicate active provider");
	finish_server(&server);
}

static void test_cycle(void) {
	static const char *const provides_a[] = {"cap.a", NULL};
	static const char *const provides_b[] = {"cap.b", NULL};
	static const char *const requires_a[] = {"cap.b", NULL};
	static const char *const requires_b[] = {"cap.a", NULL};
	static const struct shady_module a = {
		.name = "cycle-a",
		.provides = provides_a,
		.requires = requires_a,
	};
	static const struct shady_module b = {
		.name = "cycle-b",
		.provides = provides_b,
		.requires = requires_b,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &a), "register cycle a");
	expect_true(shady_modules_register(&server.modules, &b), "register cycle b");
	expect_false(shady_modules_resolve(&server), "reject dependency cycle");
	finish_server(&server);
}

static void test_enable_override(void) {
	static const char *const provides_feature[] = {"cap.feature", NULL};
	static const char *const requires_feature[] = {"cap.feature", NULL};
	static const struct shady_module provider = {
		.name = "disabled-provider",
		.provides = provides_feature,
		.enabled = disabled_by_default,
	};
	static const struct shady_module consumer = {
		.name = "feature-consumer",
		.requires = requires_feature,
	};

	struct shady_server server;
	reset_server(&server);
	expect_true(shady_modules_register(&server.modules, &provider),
		"register disabled provider");
	expect_true(shady_modules_register(&server.modules, &consumer),
		"register feature consumer");
	expect_false(shady_modules_resolve(&server),
		"disabled provider does not satisfy dependency");
	expect_true(shady_modules_set_enabled(&server.modules, "disabled-provider", true),
		"force-enable provider");
	expect_true(shady_modules_resolve(&server),
		"forced provider satisfies dependency");
	expect_order(&server, "disabled-provider", "feature-consumer",
		"forced provider precedes consumer");
	finish_server(&server);
}

int main(void) {
	wlr_log_init(WLR_ERROR, NULL);

	test_dependency_order();
	test_missing_required();
	test_optional_missing();
	test_duplicate_provider();
	test_cycle();
	test_enable_override();

	if (failures != 0) {
		fprintf(stderr, "module-resolver: %d failure(s)\n", failures);
		return 1;
	}
	puts("module-resolver: PASS");
	return 0;
}
