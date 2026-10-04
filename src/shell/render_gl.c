#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core.h"
#include "render.h"

#if SHELL_HAS_GL

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-egl.h>

/*
 * One EGL context shared by every surface. Each surface has its own
 * wl_egl_window and EGLSurface; its cairo drawing is uploaded to a texture
 * and composited with shaders: the surface's shader (or a plain copy) over
 * the whole surface, then each widget shader over its widget's rectangle.
 * Swap interval 0: pacing comes from the core's frame callbacks, so
 * eglSwapBuffers never blocks the loop.
 *
 * Cairo's ARGB32 is premultiplied BGRA in memory on little-endian hosts. It
 * is uploaded as BGRA where GL_EXT_texture_format_BGRA8888 exists and
 * swizzled on the CPU otherwise, so shaders always sample premultiplied
 * RGBA. Premultiplied output is what Wayland expects from EGL buffers.
 */

/* A compiled fragment shader with the host vertex shader. */
struct gl_program {
    struct gl_program *next;
    char *path; /* NULL for the built-in copy shader */
    GLuint program;
    bool failed;
    bool uses_time;
    GLint u_tex, u_time, u_resolution, u_size, u_rect, u_scale, u_mouse, u_hover,
        u_radius, u_quad;
};

struct gl_renderer {
    struct shell_renderer base;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    struct gl_program *programs;
    bool bgra_checked, bgra;
    unsigned char *swizzle;
    size_t swizzle_size;
    struct timespec start;
};

struct gl_surface {
    struct wl_egl_window *window;
    EGLSurface egl_surface;
    cairo_surface_t *image;
    GLuint texture;
    int width, height; /* buffer size of window/image/texture */
    int texture_width, texture_height;
    bool swap_interval_set;
};

/* Places a quad over u_quad (x, y, w, h in surface uv, y down). v_uv is the
 * surface uv, for sampling u_tex; v_local runs 0..1 across the quad. */
static const char *vertex_source =
    "attribute vec2 a_pos;\n"
    "uniform vec4 u_quad;\n"
    "varying vec2 v_uv;\n"
    "varying vec2 v_local;\n"
    "void main() {\n"
    "    v_local = a_pos;\n"
    "    v_uv = u_quad.xy + a_pos * u_quad.zw;\n"
    "    gl_Position = vec4(v_uv.x * 2.0 - 1.0, 1.0 - v_uv.y * 2.0, 0.0, 1.0);\n"
    "}\n";

static const char *copy_source =
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv);\n"
    "}\n";

static const char *fragment_prelude =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "#line 1\n";

static GLuint compile(GLenum type, const char *const *sources, int count, const char *what) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, count, sources, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shady-shell: shader %s failed to compile:\n%s\n", what, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *data = NULL;
    size_t size = 0, used = 0;
    for (;;) {
        if (used + 4096 + 1 > size) {
            size = size ? size * 2 : 8192;
            if (size > 1024 * 1024) break; /* not a shader */
            char *grown = realloc(data, size);
            if (!grown) break;
            data = grown;
        }
        size_t n = fread(data + used, 1, size - used - 1, f);
        used += n;
        if (n == 0) {
            fclose(f);
            data[used] = '\0';
            return data;
        }
    }
    fclose(f);
    free(data);
    return NULL;
}

/* Programs need a current context, so they are built on first use. */
static struct gl_program *get_program(struct gl_renderer *gl, const char *path) {
    for (struct gl_program *p = gl->programs; p; p = p->next)
        if ((!p->path && !path) || (p->path && path && !strcmp(p->path, path)))
            return p->failed ? NULL : p;

    struct gl_program *p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->path = path ? strdup(path) : NULL;
    p->next = gl->programs;
    gl->programs = p;

    char *source = path ? read_file(path) : NULL;
    if (path && !source) {
        fprintf(stderr, "shady-shell: cannot read shader %s\n", path);
        p->failed = true;
        return NULL;
    }
    const char *what = path ? path : "copy";
    const char *fragment[] = { fragment_prelude, path ? source : copy_source };
    GLuint vs = compile(GL_VERTEX_SHADER, &vertex_source, 1, "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment, 2, what);
    free(source);
    GLuint program = vs && fs ? glCreateProgram() : 0;
    if (program) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_pos");
        glLinkProgram(program);
        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            glGetProgramInfoLog(program, sizeof(log), NULL, log);
            fprintf(stderr, "shady-shell: shader %s failed to link:\n%s\n", what, log);
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!program) {
        p->failed = true;
        return NULL;
    }
    p->program = program;
    p->u_tex = glGetUniformLocation(program, "u_tex");
    p->u_time = glGetUniformLocation(program, "u_time");
    p->u_resolution = glGetUniformLocation(program, "u_resolution");
    p->u_size = glGetUniformLocation(program, "u_size");
    p->u_rect = glGetUniformLocation(program, "u_rect");
    p->u_scale = glGetUniformLocation(program, "u_scale");
    p->u_mouse = glGetUniformLocation(program, "u_mouse");
    p->u_hover = glGetUniformLocation(program, "u_hover");
    p->u_radius = glGetUniformLocation(program, "u_radius");
    p->u_quad = glGetUniformLocation(program, "u_quad");
    p->uses_time = p->u_time >= 0;
    if (path) fprintf(stderr, "shady-shell: compiled shader %s%s\n", path,
        p->uses_time ? " (animated)" : "");
    return p;
}

static void free_programs(struct gl_renderer *gl, bool keep_copy) {
    struct gl_program **link = &gl->programs;
    while (*link) {
        struct gl_program *p = *link;
        if (keep_copy && !p->path) {
            link = &p->next;
            continue;
        }
        *link = p->next;
        if (p->program) glDeleteProgram(p->program);
        free(p->path);
        free(p);
    }
}

static float seconds_since(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (float)(now.tv_sec - start->tv_sec) + (float)(now.tv_nsec - start->tv_nsec) * 1e-9f;
}

/* Draw one effect's quad. A NULL effect is the plain copy over the surface. */
static bool draw_effect(struct gl_renderer *gl, struct shell_surface *surface,
        const struct shell_effect *effect, float time) {
    struct gl_program *p = get_program(gl, effect ? effect->shader : NULL);
    if (!p) return false;
    double sw = surface->width, sh = surface->height;
    double x = 0, y = 0, w = sw, h = sh, radius = 0;
    if (effect && (effect->width > 0 || effect->height > 0)) {
        x = effect->x; y = effect->y; w = effect->width; h = effect->height;
        radius = effect->radius;
    }
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return false;
    struct shell_core *core = surface->core;
    bool pointer = core->pointer_focus == surface;
    double mx = pointer ? core->pointer_x - x : -1, my = pointer ? core->pointer_y - y : -1;
    bool hover = pointer && mx >= 0 && my >= 0 && mx < w && my < h;

    glUseProgram(p->program);
    glUniform1i(p->u_tex, 0);
    glUniform4f(p->u_quad, (float)(x / sw), (float)(y / sh), (float)(w / sw), (float)(h / sh));
    if (p->u_time >= 0) glUniform1f(p->u_time, time);
    if (p->u_resolution >= 0)
        glUniform2f(p->u_resolution, (float)(w * surface->scale), (float)(h * surface->scale));
    if (p->u_size >= 0) glUniform2f(p->u_size, (float)w, (float)h);
    if (p->u_rect >= 0) glUniform4f(p->u_rect, (float)x, (float)y, (float)w, (float)h);
    if (p->u_scale >= 0) glUniform1f(p->u_scale, (float)surface->scale);
    if (p->u_mouse >= 0) glUniform2f(p->u_mouse, (float)mx, (float)my);
    if (p->u_hover >= 0) glUniform1f(p->u_hover, hover ? 1.0f : 0.0f);
    if (p->u_radius >= 0) glUniform1f(p->u_radius, (float)radius);
    for (size_t i = 0; effect && i < effect->uniform_count; i++) {
        const struct shell_uniform *u = &effect->uniforms[i];
        GLint loc = glGetUniformLocation(p->program, u->name);
        if (loc < 0) continue;
        switch (u->size) {
        case 1: glUniform1fv(loc, 1, u->value); break;
        case 2: glUniform2fv(loc, 1, u->value); break;
        case 3: glUniform3fv(loc, 1, u->value); break;
        default: glUniform4fv(loc, 1, u->value); break;
        }
    }
    static const GLfloat quad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    return p->uses_time;
}

static bool make_current(struct gl_renderer *gl, struct gl_surface *state) {
    if (!eglMakeCurrent(gl->display, state->egl_surface, state->egl_surface, gl->context)) {
        fprintf(stderr, "shady-shell: eglMakeCurrent failed (0x%x)\n", eglGetError());
        return false;
    }
    if (!state->swap_interval_set) {
        eglSwapInterval(gl->display, 0);
        state->swap_interval_set = true;
    }
    if (!gl->bgra_checked) {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        gl->bgra = ext && strstr(ext, "GL_EXT_texture_format_BGRA8888");
        gl->bgra_checked = true;
    }
    return true;
}

/* Draw the surface from its texture with its effects, then swap. */
static bool composite_surface(struct gl_renderer *gl, struct shell_surface *surface,
        struct gl_surface *state) {
    float time = seconds_since(&gl->start);
    const struct shell_effects *effects = &surface->effects;
    glViewport(0, 0, state->width, state->height);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->texture);

    bool animated = false;
    if (effects->surface.shader) {
        bool drew = get_program(gl, effects->surface.shader) != NULL;
        animated |= drew && draw_effect(gl, surface, &effects->surface, time);
        if (!drew) draw_effect(gl, surface, NULL, time);
    } else {
        draw_effect(gl, surface, NULL, time);
    }
    /* Widget shaders replace their rectangle; they sample u_tex for the
     * content underneath. */
    for (size_t i = 0; i < effects->count; i++)
        animated |= draw_effect(gl, surface, &effects->widgets[i], time);
    surface->animated = animated;

    if (!eglSwapBuffers(gl->display, state->egl_surface)) {
        fprintf(stderr, "shady-shell: eglSwapBuffers failed (0x%x)\n", eglGetError());
        return false;
    }
    return true;
}

static void upload(struct gl_renderer *gl, struct gl_surface *state) {
    const unsigned char *pixels = cairo_image_surface_get_data(state->image);
    GLenum format = GL_RGBA;
    if (gl->bgra) {
        format = GL_BGRA_EXT;
    } else {
        size_t size = (size_t)state->width * (size_t)state->height * 4;
        if (size > gl->swizzle_size) {
            unsigned char *grown = realloc(gl->swizzle, size);
            if (!grown) return;
            gl->swizzle = grown;
            gl->swizzle_size = size;
        }
        for (size_t i = 0; i < size; i += 4) {
            gl->swizzle[i] = pixels[i + 2];
            gl->swizzle[i + 1] = pixels[i + 1];
            gl->swizzle[i + 2] = pixels[i];
            gl->swizzle[i + 3] = pixels[i + 3];
        }
        pixels = gl->swizzle;
    }
    if (!state->texture) {
        glGenTextures(1, &state->texture);
        glBindTexture(GL_TEXTURE_2D, state->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, state->texture);
    }
    /* ARGB32 rows are exactly width * 4 bytes, so no row length is needed. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (state->texture_width != state->width || state->texture_height != state->height) {
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)format, state->width, state->height, 0,
            format, GL_UNSIGNED_BYTE, pixels);
        state->texture_width = state->width;
        state->texture_height = state->height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state->width, state->height,
            format, GL_UNSIGNED_BYTE, pixels);
    }
}

static void gl_surface_release(struct gl_renderer *gl, struct gl_surface *state) {
    if (state->texture || state->egl_surface != EGL_NO_SURFACE)
        eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, gl->context);
    if (state->texture) glDeleteTextures(1, &state->texture);
    state->texture = 0;
    eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (state->egl_surface != EGL_NO_SURFACE) eglDestroySurface(gl->display, state->egl_surface);
    state->egl_surface = EGL_NO_SURFACE;
    if (state->window) wl_egl_window_destroy(state->window);
    state->window = NULL;
    if (state->image) cairo_surface_destroy(state->image);
    state->image = NULL;
}

static cairo_surface_t *gl_begin(struct shell_renderer *renderer,
        struct shell_surface *surface, int width, int height) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    struct gl_surface *state = surface->render_data;
    if (!state) {
        state = calloc(1, sizeof(*state));
        if (!state) return NULL;
        state->egl_surface = EGL_NO_SURFACE;
        surface->render_data = state;
    }
    if (!state->window) {
        state->window = wl_egl_window_create(surface->wl_surface, width, height);
        if (!state->window) return NULL;
        state->egl_surface = eglCreateWindowSurface(gl->display, gl->config,
            (EGLNativeWindowType)state->window, NULL);
        if (state->egl_surface == EGL_NO_SURFACE) {
            fprintf(stderr, "shady-shell: eglCreateWindowSurface failed (0x%x)\n",
                eglGetError());
            wl_egl_window_destroy(state->window);
            state->window = NULL;
            return NULL;
        }
    } else if (state->width != width || state->height != height) {
        wl_egl_window_resize(state->window, width, height, 0, 0);
    }
    if (!state->image || state->width != width || state->height != height) {
        if (state->image) cairo_surface_destroy(state->image);
        state->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        if (cairo_surface_status(state->image) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(state->image);
            state->image = NULL;
            return NULL;
        }
    }
    state->width = width;
    state->height = height;
    return state->image;
}

static bool gl_end(struct shell_renderer *renderer, struct shell_surface *surface) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    struct gl_surface *state = surface->render_data;
    if (!state || !state->image || !make_current(gl, state)) return false;
    upload(gl, state);
    return composite_surface(gl, surface, state);
}

static bool gl_composite(struct shell_renderer *renderer, struct shell_surface *surface) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    struct gl_surface *state = surface->render_data;
    if (!state || !state->texture || !make_current(gl, state)) return false;
    return composite_surface(gl, surface, state);
}

static void gl_forget_shaders(struct shell_renderer *renderer) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    if (gl->programs && eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, gl->context))
        free_programs(gl, true);
}

static void gl_surface_finish(struct shell_renderer *renderer, struct shell_surface *surface) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    struct gl_surface *state = surface->render_data;
    if (!state) return;
    gl_surface_release(gl, state);
    free(state);
    surface->render_data = NULL;
}

static void gl_destroy(struct shell_renderer *renderer) {
    struct gl_renderer *gl = (struct gl_renderer *)renderer;
    if (gl->display != EGL_NO_DISPLAY) {
        if (gl->programs && eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                gl->context))
            free_programs(gl, false);
        eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (gl->context != EGL_NO_CONTEXT) eglDestroyContext(gl->display, gl->context);
        eglTerminate(gl->display);
    }
    free(gl->swizzle);
    free(gl);
}

static const struct shell_renderer_impl gl_impl = {
    .name = "gl",
    .destroy = gl_destroy,
    .begin = gl_begin,
    .end = gl_end,
    .composite = gl_composite,
    .surface_finish = gl_surface_finish,
    .forget_shaders = gl_forget_shaders,
};

struct shell_renderer *shell_renderer_gl_create(struct shell_core *core) {
    struct gl_renderer *gl = calloc(1, sizeof(*gl));
    if (!gl) return NULL;
    gl->base.impl = &gl_impl;
    gl->base.core = core;
    gl->context = EGL_NO_CONTEXT;
    clock_gettime(CLOCK_MONOTONIC, &gl->start);

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    gl->display = get_platform_display
        ? get_platform_display(EGL_PLATFORM_WAYLAND_KHR, core->display, NULL)
        : eglGetDisplay((EGLNativeDisplayType)core->display);
    EGLint major = 0, minor = 0;
    if (gl->display == EGL_NO_DISPLAY || !eglInitialize(gl->display, &major, &minor)) {
        fprintf(stderr, "shady-shell: EGL unavailable (0x%x)\n", eglGetError());
        gl->display = EGL_NO_DISPLAY;
        gl_destroy(&gl->base);
        return NULL;
    }
    static const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint count = 0;
    static const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
            !eglChooseConfig(gl->display, config_attribs, &gl->config, 1, &count) ||
            count < 1 ||
            (gl->context = eglCreateContext(gl->display, gl->config, EGL_NO_CONTEXT,
                context_attribs)) == EGL_NO_CONTEXT) {
        fprintf(stderr, "shady-shell: no usable EGL config/context (0x%x)\n", eglGetError());
        gl_destroy(&gl->base);
        return NULL;
    }
    return &gl->base;
}

#else

struct shell_renderer *shell_renderer_gl_create(struct shell_core *core) {
    (void)core;
    return NULL;
}

#endif

struct shell_renderer *shell_renderer_create(struct shell_core *core) {
    const char *choice = getenv("SHADY_SHELL_RENDERER");
    if (choice && !strcmp(choice, "shm")) return shell_renderer_shm_create(core);
    struct shell_renderer *renderer = shell_renderer_gl_create(core);
    if (renderer) return renderer;
    if (choice && !strcmp(choice, "gl")) {
        fprintf(stderr, "shady-shell: SHADY_SHELL_RENDERER=gl but GL is unavailable\n");
        return NULL;
    }
    return shell_renderer_shm_create(core);
}
