#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "render.h"

#if SHELL_HAS_GL

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wayland-egl.h>

/*
 * One EGL context shared by every surface. Each surface has its own
 * wl_egl_window and EGLSurface; its cairo drawing is uploaded to a texture
 * and drawn as a fullscreen quad. Swap interval 0: pacing comes from the
 * core's frame callbacks, so eglSwapBuffers never blocks the loop.
 *
 * Cairo's ARGB32 is premultiplied BGRA in memory on little-endian hosts. It
 * is uploaded as RGBA and swizzled in the shader, which avoids depending on
 * GL_EXT_texture_format_BGRA8888. Premultiplied output is what Wayland
 * expects from EGL buffers.
 */

struct gl_renderer {
    struct shell_renderer base;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    GLuint program;
    GLint u_tex;
    bool program_failed;
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

static const char *vertex_source =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(a_pos.x * 0.5 + 0.5, 0.5 - a_pos.y * 0.5);\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *fragment_source =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv).bgra;\n"
    "}\n";

static GLuint compile(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "shady-shell: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

/* Programs need a current context, so they are built on the first paint. */
static bool ensure_program(struct gl_renderer *gl) {
    if (gl->program) return true;
    if (gl->program_failed) return false;
    GLuint vs = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    GLuint program = vs && fs ? glCreateProgram() : 0;
    if (program) {
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "a_pos");
        glLinkProgram(program);
        GLint ok = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            fprintf(stderr, "shady-shell: shader link failed\n");
            glDeleteProgram(program);
            program = 0;
        }
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!program) {
        gl->program_failed = true;
        return false;
    }
    gl->program = program;
    gl->u_tex = glGetUniformLocation(program, "u_tex");
    return true;
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
    if (!state || !state->image) return false;
    if (!eglMakeCurrent(gl->display, state->egl_surface, state->egl_surface, gl->context)) {
        fprintf(stderr, "shady-shell: eglMakeCurrent failed (0x%x)\n", eglGetError());
        return false;
    }
    if (!state->swap_interval_set) {
        eglSwapInterval(gl->display, 0);
        state->swap_interval_set = true;
    }
    if (!ensure_program(gl)) return false;

    const unsigned char *pixels = cairo_image_surface_get_data(state->image);
    if (!state->texture) {
        glGenTextures(1, &state->texture);
        glBindTexture(GL_TEXTURE_2D, state->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, state->texture);
    }
    /* ARGB32 rows are exactly width * 4 bytes, so no row length is needed. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (state->texture_width != state->width || state->texture_height != state->height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state->width, state->height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        state->texture_width = state->width;
        state->texture_height = state->height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, state->width, state->height,
            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    glViewport(0, 0, state->width, state->height);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(gl->program);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(gl->u_tex, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);

    if (!eglSwapBuffers(gl->display, state->egl_surface)) {
        fprintf(stderr, "shady-shell: eglSwapBuffers failed (0x%x)\n", eglGetError());
        return false;
    }
    return true;
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
        if (gl->program && eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                gl->context))
            glDeleteProgram(gl->program);
        eglMakeCurrent(gl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (gl->context != EGL_NO_CONTEXT) eglDestroyContext(gl->display, gl->context);
        eglTerminate(gl->display);
    }
    free(gl);
}

static const struct shell_renderer_impl gl_impl = {
    .name = "gl",
    .destroy = gl_destroy,
    .begin = gl_begin,
    .end = gl_end,
    .surface_finish = gl_surface_finish,
};

struct shell_renderer *shell_renderer_gl_create(struct shell_core *core) {
    struct gl_renderer *gl = calloc(1, sizeof(*gl));
    if (!gl) return NULL;
    gl->base.impl = &gl_impl;
    gl->base.core = core;
    gl->context = EGL_NO_CONTEXT;

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
