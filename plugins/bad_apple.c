/*
 * bad-apple: Super+A turns the screen into a flip-dot display and plays a
 * video on it. Every tile is a little card with the desktop printed on its
 * front and black on its back; a tile turns over when the video goes black
 * there and back when it goes white, so the shadows of the video sweep across
 * the desktop as waves of turning cards. A wave from the centre turns the
 * first frame in and, at the end, turns the desktop back.
 *
 * No video ships with Shady: tools/bad-apple.sh turns your own copy into
 * frames.bin (one bit per tile) and audio.wav in
 * $SHADY_BAD_APPLE_DIR (default $XDG_DATA_HOME/shady/bad-apple). The audio
 * plays through the first of pw-play, paplay and aplay, or the command in
 * $SHADY_BAD_APPLE_PLAYER; the frames follow the wall clock from the moment
 * it starts, shifted by $SHADY_BAD_APPLE_DELAY_MS for slow audio.
 *
 * One post-process (bad_apple.frag, shader_draw_fullscreen_scene) draws the
 * tiles over the finished frame. The tile angles are computed here and sent
 * as a small luminance texture on texture unit 1, which the host leaves
 * alone.
 *
 * Requires the spatial module (GL renderer).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

#include <GLES2/gl2.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/plugin.h>

extern char **environ;

#define MAX_GRID 512
/* A tile takes this long to turn over. */
#define FLIP_SECONDS 0.09f
/* The opening and closing waves reach the corners after this long. */
#define WAVE_SECONDS 0.9f
/* The tile grid fades in and out over this long at either end. */
#define FADE_SECONDS 0.3f

enum phase { PHASE_IDLE, PHASE_INTRO, PHASE_PLAYING, PHASE_OUTRO };

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program program;
static shady_render_hook_id hook;

static struct {
	enum phase phase;
	/* frames.bin, its header parsed. */
	uint8_t *file;
	const uint8_t *frames;
	int cols, rows;
	size_t stride, frame_bytes, frame_count;
	double fps;
	/* Per tile: angle in half turns (0 the desktop, 1 black), the wave's
	 * delay in seconds and the texel sent to the shader. */
	float *angle, *delay;
	uint8_t *texels;
	bool dirty;
	/* Seconds into the current phase (intro, outro). */
	float clock;
	struct timespec started;
	double delay_seconds;
	/* The frame the tiles are turning to. */
	size_t frame;
	pid_t audio;
	/* GL texture, made and deleted inside render hooks. */
	GLuint texture;
	bool release_texture;
} ba;

static void log_info(const char *message) {
	api->log(SHADY_PLUGIN_LOG_INFO, message);
}

static void log_error(const char *message) {
	api->log(SHADY_PLUGIN_LOG_ERROR, message);
}

static double since(const struct timespec *start) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) * 1e-9;
}

static void data_path(char *out, size_t size, const char *name) {
	const char *dir = getenv("SHADY_BAD_APPLE_DIR");
	const char *xdg = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (dir && *dir) snprintf(out, size, "%s/%s", dir, name);
	else if (xdg && *xdg) snprintf(out, size, "%s/shady/bad-apple/%s", xdg, name);
	else snprintf(out, size, "%s/.local/share/shady/bad-apple/%s", home ? home : ".", name);
}

/* ---- frames ----------------------------------------------------------- */

static void unload(void) {
	free(ba.file);
	free(ba.angle);
	free(ba.delay);
	free(ba.texels);
	ba.file = NULL;
	ba.frames = NULL;
	ba.angle = ba.delay = NULL;
	ba.texels = NULL;
	ba.frame_count = 0;
}

static bool load(void) {
	char path[1024], message[1200];
	data_path(path, sizeof(path), "frames.bin");
	FILE *f = fopen(path, "rb");
	if (!f) {
		snprintf(message, sizeof(message),
			"bad-apple: no frames at %s; make them with tools/bad-apple.sh <video>", path);
		log_error(message);
		return false;
	}
	struct stat st;
	uint8_t *data = NULL;
	if (fstat(fileno(f), &st) == 0 && st.st_size > 0 && (data = malloc((size_t)st.st_size)) &&
			fread(data, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
		free(data);
		data = NULL;
	}
	fclose(f);
	if (!data) {
		log_error("bad-apple: cannot read frames.bin");
		return false;
	}

	size_t size = (size_t)st.st_size;
	const uint8_t *newline = memchr(data, '\n', size < 128 ? size : 128);
	int version = 0, cols = 0, rows = 0, num = 0, den = 0;
	char header[128] = {0};
	if (newline) memcpy(header, data, (size_t)(newline - data));
	if (!newline || sscanf(header, "shady-bad-apple %d %d %d %d %d",
			&version, &cols, &rows, &num, &den) != 5 || version != 1 ||
			cols < 1 || rows < 1 || cols > MAX_GRID || rows > MAX_GRID || num < 1 || den < 1) {
		log_error("bad-apple: frames.bin has no valid header; remake it with tools/bad-apple.sh");
		free(data);
		return false;
	}
	size_t offset = (size_t)(newline - data) + 1;
	size_t stride = ((size_t)cols + 7) / 8;
	size_t frame_bytes = stride * (size_t)rows;
	size_t count = (size - offset) / frame_bytes;
	size_t tiles = (size_t)cols * (size_t)rows;
	float *angle = calloc(tiles, sizeof(float));
	float *delay = calloc(tiles, sizeof(float));
	uint8_t *texels = calloc(tiles, 1);
	if (!count || !angle || !delay || !texels) {
		log_error(count ? "bad-apple: out of memory" : "bad-apple: frames.bin holds no frames");
		free(data);
		free(angle);
		free(delay);
		free(texels);
		return false;
	}

	unload();
	ba.file = data;
	ba.frames = data + offset;
	ba.cols = cols;
	ba.rows = rows;
	ba.stride = stride;
	ba.frame_bytes = frame_bytes;
	ba.frame_count = count;
	ba.fps = (double)num / (double)den;
	ba.angle = angle;
	ba.delay = delay;
	ba.texels = texels;
	/* The waves start in the middle and reach the corners last. */
	float cx = (float)(cols - 1) * 0.5f, cy = (float)(rows - 1) * 0.5f;
	float far = sqrtf(cx * cx + cy * cy);
	for (int y = 0; y < rows; y++)
		for (int x = 0; x < cols; x++) {
			float dx = (float)x - cx, dy = (float)y - cy;
			delay[y * cols + x] = far > 0.f ? sqrtf(dx * dx + dy * dy) / far * WAVE_SECONDS : 0.f;
		}
	snprintf(message, sizeof(message), "bad-apple: %zu frames of %dx%d at %.3f fps",
		count, cols, rows, ba.fps);
	log_info(message);
	return true;
}

static bool black(size_t frame, int x, int y) {
	const uint8_t *row = ba.frames + frame * ba.frame_bytes + (size_t)y * ba.stride;
	return (row[x >> 3] >> (7 - (x & 7))) & 1;
}

/* ---- audio ------------------------------------------------------------ */

static pid_t spawn(char *const argv[]) {
	pid_t pid;
	if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) return 0;
	return pid;
}

static void start_audio(void) {
	char path[1024];
	data_path(path, sizeof(path), "audio.wav");
	struct stat st;
	if (stat(path, &st) != 0) {
		log_info("bad-apple: no audio.wav; playing without sound");
		return;
	}
	const char *custom = getenv("SHADY_BAD_APPLE_PLAYER");
	if (custom && *custom) {
		/* The shell parses the command as written, quotes included, and
		 * hands it the file as "$0". */
		char command[2048];
		snprintf(command, sizeof(command), "exec %s \"$0\"", custom);
		char *argv[] = { "sh", "-c", command, path, NULL };
		ba.audio = spawn(argv);
	} else {
		static const char *const players[] = { "pw-play", "paplay", "aplay", NULL };
		for (const char *const *player = players; *player && !ba.audio; player++) {
			char *argv[] = { (char *)*player, path, NULL };
			ba.audio = spawn(argv);
		}
	}
	if (!ba.audio) log_error("bad-apple: no audio player found (pw-play, paplay, aplay)");
}

/* The compositor reaps its children; this only forgets ours once it is gone. */
static void stop_audio(void) {
	if (ba.audio > 0) kill(ba.audio, SIGTERM);
	ba.audio = 0;
}

/* ---- playback --------------------------------------------------------- */

static void begin_outro(void) {
	stop_audio();
	ba.phase = PHASE_OUTRO;
	ba.clock = 0.f;
}

static void begin(void) {
	if (!load()) return;
	const char *delay = getenv("SHADY_BAD_APPLE_DELAY_MS");
	ba.delay_seconds = delay && *delay ? atof(delay) / 1000.0 : 0.0;
	ba.phase = PHASE_INTRO;
	ba.clock = 0.f;
	ba.frame = 0;
	ba.dirty = true;
	ba.release_texture = false;
	log_info("bad-apple: playing (Super+A stops)");
}

/* Turn every tile toward its target: black where the frame is black, but
 * during the intro only once the wave has reached it, and during the outro
 * back to the desktop once it has passed. */
static void turn_tiles(float dt) {
	float step = dt / FLIP_SECONDS;
	for (int y = 0; y < ba.rows; y++)
		for (int x = 0; x < ba.cols; x++) {
			size_t i = (size_t)y * (size_t)ba.cols + (size_t)x;
			bool reached = ba.clock >= ba.delay[i];
			bool want = black(ba.frame, x, y);
			if (ba.phase == PHASE_INTRO && !reached) want = false;
			if (ba.phase == PHASE_OUTRO && reached) want = false;
			float target = want ? 1.f : 0.f;
			float a = ba.angle[i];
			a = a < target ? fminf(a + step, target) : fmaxf(a - step, target);
			ba.angle[i] = a;
			ba.texels[i] = (uint8_t)lrintf(a * 255.f);
		}
	ba.dirty = true;
}

static void tick(struct shady_server *server, float dt, float logical_w, float logical_h) {
	(void)server;
	(void)logical_w;
	(void)logical_h;
	if (ba.phase == PHASE_IDLE) return;
	if (dt < 0.f) dt = 0.f;
	if (dt > 0.1f) dt = 0.1f;
	ba.clock += dt;

	switch (ba.phase) {
	case PHASE_INTRO:
		if (ba.clock >= WAVE_SECONDS + FLIP_SECONDS) {
			ba.phase = PHASE_PLAYING;
			start_audio();
			clock_gettime(CLOCK_MONOTONIC, &ba.started);
		}
		break;
	case PHASE_PLAYING: {
		double t = since(&ba.started) - ba.delay_seconds;
		size_t frame = t > 0.0 ? (size_t)(t * ba.fps) : 0;
		if (frame >= ba.frame_count) {
			begin_outro();
			log_info("bad-apple: finished");
		} else {
			ba.frame = frame;
		}
		break;
	}
	case PHASE_OUTRO:
		if (ba.clock >= WAVE_SECONDS + FLIP_SECONDS + FADE_SECONDS) {
			ba.phase = PHASE_IDLE;
			ba.release_texture = true;
			api->schedule_render(host);
			return;
		}
		break;
	case PHASE_IDLE:
		break;
	}
	turn_tiles(dt);
	api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms, int nsyms,
		uint32_t state, uint32_t modifiers) {
	(void)server;
	if (state != SHADY_KEY_PRESSED || !(modifiers & SHADY_MODIFIER_LOGO) ||
			(modifiers & (SHADY_MODIFIER_SHIFT | SHADY_MODIFIER_CTRL | SHADY_MODIFIER_ALT)))
		return false;
	bool pressed = false;
	for (int i = 0; i < nsyms; i++) pressed |= syms[i] == XKB_KEY_a || syms[i] == XKB_KEY_A;
	if (!pressed) return false;
	if (ba.phase == PHASE_IDLE) begin();
	else if (ba.phase != PHASE_OUTRO) {
		begin_outro();
		log_info("bad-apple: stopped");
	}
	api->schedule_render(host);
	return true;
}

/* ---- drawing ---------------------------------------------------------- */

static float grid_fade(void) {
	switch (ba.phase) {
	case PHASE_INTRO: return fminf(ba.clock / FADE_SECONDS, 1.f);
	case PHASE_PLAYING: return 1.f;
	case PHASE_OUTRO: {
		float left = WAVE_SECONDS + FLIP_SECONDS + FADE_SECONDS - ba.clock;
		return fmaxf(0.f, fminf(left / FADE_SECONDS, 1.f));
	}
	case PHASE_IDLE: break;
	}
	return 0.f;
}

static void draw(shady_host h, const struct shady_render_context *ctx, void *user) {
	(void)user;
	if (ba.release_texture) {
		if (ba.texture) glDeleteTextures(1, &ba.texture);
		ba.texture = 0;
		ba.release_texture = false;
		unload();
	}
	if (ba.phase == PHASE_IDLE || !program || !ctx || !ba.texels) return;

	GLint old_active = 0, old_texture = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	if (!ba.texture) {
		glGenTextures(1, &ba.texture);
		glBindTexture(GL_TEXTURE_2D, ba.texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		ba.dirty = true;
	}
	glBindTexture(GL_TEXTURE_2D, ba.texture);
	if (ba.dirty) {
		GLint old_align = 4;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &old_align);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, ba.cols, ba.rows, 0,
			GL_LUMINANCE, GL_UNSIGNED_BYTE, ba.texels);
		glPixelStorei(GL_UNPACK_ALIGNMENT, old_align);
		ba.dirty = false;
	}

	api->shader_uniform_int(h, program, "u_tiles", 1);
	api->shader_uniform_vec2(h, program, "u_grid", (float)ba.cols, (float)ba.rows);
	api->shader_uniform_float(h, program, "u_fade", grid_fade());
	static bool reported;
	bool drawn = api->shader_draw_fullscreen_scene(h, program);
	if (!reported) {
		reported = true;
		api->log(drawn ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR, drawn
			? "bad-apple: drawing the tiles"
			: "bad-apple: scene capture failed; the tiles cannot be drawn");
	}

	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	glActiveTexture((GLenum)old_active);
}

/* ---- lifecycle -------------------------------------------------------- */

static void destroy(struct shady_server *server);

static bool init(struct shady_server *server) {
	const char *root = getenv("SHADY_ROOT");
	if (!root || !*root) root = ".";
	char vert[1024], frag[1024];
	snprintf(vert, sizeof(vert), "%s/plugins/shaders/overlay.vert", root);
	snprintf(frag, sizeof(frag), "%s/plugins/shaders/bad_apple.frag", root);
	program = api->shader_program_create(host, vert, frag);
	/* Last, so the tiles carry the shell's bars and panels too. */
	hook = api->render_hook_add(host, SHADY_RENDER_STAGE_OVERLAY, draw, NULL);
	if (!program || !hook) {
		log_error("bad-apple: failed to build the shader or the render hook");
		destroy(server);
		return false;
	}
	return true;
}

static void start(struct shady_server *server) {
	(void)server;
	log_info("bad-apple: Super+A plays frames.bin on the desktop");
}

static void stop(struct shady_server *server) {
	(void)server;
	stop_audio();
	ba.phase = PHASE_IDLE;
}

/* The texture needs the GL context, which only render hooks are sure to
 * have; unloading mid-show leaves that one small texture behind. */
static void destroy(struct shady_server *server) {
	(void)server;
	stop_audio();
	unload();
	if (hook) api->render_hook_remove(host, hook);
	if (program) api->shader_program_destroy(host, program);
	hook = 0;
	program = 0;
	memset(&ba, 0, sizeof(ba));
}

static const char *const provides[] = { "spatial.bad-apple", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module module = {
	.name = "bad-apple",
	.provides = provides,
	.requires = requires,
	.init = init,
	.start = start,
	.stop = stop,
	.destroy = destroy,
	.key = key,
	.tick = tick,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api, shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 || !host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			!SHADY_API_HAS(host_api, shader_draw_fullscreen_scene) ||
			!SHADY_API_HAS(host_api, render_hook_add))
		return NULL;
	api = host_api;
	host = host_handle;
	if (SHADY_API_HAS(api, declare_shortcut))
		api->declare_shortcut(host, "Super+A", "Play Bad Apple on the desktop as flip-dot tiles");
	return &module;
}
