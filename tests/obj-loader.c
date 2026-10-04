/* Exercise the OBJ environment loader plugin through its public contract:
 * dlopen it, capture the loader it registers, and parse sample files. */
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <shady/environment.h>
#include <shady/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int host_object;
#define HOST ((shady_host)&host_object)

static const struct shady_environment_loader *registered;

static shady_environment_loader_id register_loader(shady_host h,
        const struct shady_environment_loader *loader) {
    assert(h == HOST && !registered);
    registered = loader;
    return 7;
}
static bool unregister_loader(shady_host h, shady_environment_loader_id id) {
    assert(h == HOST && id == 7 && registered);
    registered = NULL;
    return true;
}
static const struct shady_environment_api_v1 feature = {
    .struct_size = sizeof(feature),
    .register_loader = register_loader,
    .unregister_loader = unregister_loader,
};
static const void *query_api(shady_host h, const char *name, uint32_t version) {
    assert(h == HOST);
    return !strcmp(name, SHADY_ENVIRONMENT_API) && version == 1 ? &feature : NULL;
}
static void host_log(enum shady_plugin_log_level level, const char *message) {
    (void)level;
    fprintf(stderr, "[plugin] %s\n", message);
}
static struct shady_plugin_api_v1 api = {
    .abi_version = 1, .struct_size = sizeof(api), .query_api = query_api, .log = host_log,
};

static const char *write_temp(const char *dir, const char *name, const char *text) {
    static char path[4096];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
    return path;
}

static bool load(const char *path, struct shady_environment_scene *scene) {
    *scene = (struct shady_environment_scene){ .struct_size = sizeof(*scene) };
    return registered->load(HOST, path, scene, registered->user_data);
}

static void check_test_room(const char *path) {
    struct shady_environment_scene scene;
    assert(load(path, &scene));
    /* 10 visual + 12 collision faces, all visible; one collision group. */
    assert(scene.vertex_count == 22 * 3 && !scene.indices);
    assert(scene.box_count == 1 && scene.triangle_count == 12);
    const struct shady_environment_box *box = &scene.boxes[0];
    assert(box->min[0] == -0.75f && box->max[0] == 0.75f);
    assert(box->min[1] == -0.62f && box->max[1] == 0.10f);
    assert(box->min[2] == -1.60f && box->max[2] == -0.40f);
    registered->release(HOST, &scene, registered->user_data);
}

static void check_formats(const char *dir) {
    struct shady_environment_scene scene;

    /* Quad with v/vt/vn refs, negative indices and a trailing comment:
     * fan-triangulated into two triangles keeping authored normals/uvs. */
    const char *quad = write_temp(dir, "quad.obj",
        "v 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\n"
        "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
        "vn 0 1 0\n"
        "f -4/1/1 -3/2/1 -2/3/1 -1/4/1 # floor\n");
    assert(load(quad, &scene));
    assert(scene.vertex_count == 6 && scene.box_count == 0 && scene.triangle_count == 0);
    assert(scene.vertices[0].normal[1] == 1.0f && scene.vertices[4].uv[0] == 1.0f);
    assert(scene.vertices[5].position[2] == 1.0f && scene.vertices[5].uv[1] == 1.0f);
    registered->release(HOST, &scene, registered->user_data);

    /* No normals: a flat normal is generated from the winding. Two separate
     * collision groups produce two boxes; a non-collision group ends one. */
    const char *groups = write_temp(dir, "groups.obj",
        "v 0 0 0\nv 0 0 -1\nv 1 0 0\nv 5 5 5\n"
        "o collision_a\nf 1 2 3\n"
        "o visual\nf 1 3 4\n"
        "g collision_b\nf 2//1 3 4\n");
    /* f 2//1 references a missing normal and must be rejected. */
    assert(!load(groups, &scene));
    groups = write_temp(dir, "groups.obj",
        "v 0 0 0\nv 0 0 -1\nv 1 0 0\nv 5 5 5\n"
        "o collision_a\nf 1 3 2\n"
        "o visual\nf 1 3 4\n"
        "g collision_b\nf 2 3 4\n");
    assert(load(groups, &scene));
    assert(scene.vertex_count == 9 && scene.box_count == 2 && scene.triangle_count == 2);
    assert(fabsf(scene.vertices[0].normal[1] - 1.0f) < 1e-6f);
    assert(scene.boxes[0].max[0] == 1.0f && scene.boxes[0].max[1] == 0.0f);
    assert(scene.boxes[1].max[1] == 5.0f && scene.boxes[1].min[2] == -1.0f);
    registered->release(HOST, &scene, registered->user_data);

    assert(!load(write_temp(dir, "bad.obj", "v 0 0 0\nf 1 2 3\n"), &scene));
    assert(!load(write_temp(dir, "empty.obj", "v 0 0 0\n"), &scene));
    assert(!load(write_temp(dir, "line.obj", "v 0 0 0\nv 1 0 0\nf 1 2\n"), &scene));
    assert(!load("/nonexistent/shady.obj", &scene));
}

int main(int argc, char **argv) {
    assert(argc == 4);
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    shady_plugin_entry_v1_fn entry =
        (shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
    assert(entry);
    assert(!entry(2, &api, HOST));
    const struct shady_module *module = entry(1, &api, HOST);
    assert(module && !strcmp(module->name, "obj-loader"));

    assert(module->init((struct shady_server *)HOST) && registered);
    assert(!strcmp(registered->name, "obj"));
    assert(!strcmp(registered->extensions[0], "obj") && !registered->extensions[1]);

    check_test_room(argv[2]);
    check_formats(argv[3]);

    module->destroy((struct shady_server *)HOST);
    assert(!registered);
    dlclose(handle);
    return 0;
}
