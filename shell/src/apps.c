#define _POSIX_C_SOURCE 200809L

/* Application index for the launcher: .desktop entries from the XDG data
 * directories, searched by name or command. */

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "shell.h"

static bool ascii_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
                (char)tolower((unsigned char)p[i]) ==
                (char)tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

static void sanitize_exec(char *dst, size_t dst_size, const char *src) {
    size_t out = 0;
    for (size_t i = 0; src && src[i] && out + 1 < dst_size; i++) {
        if (src[i] != '%') {
            dst[out++] = src[i];
            continue;
        }
        if (src[i + 1] == '%') {
            dst[out++] = '%';
            i++;
            continue;
        }
        if (src[i + 1]) i++;
    }
    while (out > 0 && (dst[out - 1] == ' ' || dst[out - 1] == '\t')) out--;
    dst[out] = '\0';
}

static bool desktop_truthy(const char *value) {
    return value && (!strcasecmp(value, "true") || !strcmp(value, "1"));
}

static void add_desktop_file(struct shell *shell, const char *path) {
    if (shell->app_count >= MAX_APPS) return;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024], name[APP_NAME_MAX] = {0}, exec[APP_EXEC_MAX] = {0};
    bool in_entry = false, hidden = false, nodisplay = false, application = true;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (line[0] == '[') {
            in_entry = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry || line[0] == '#' || !line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = '\0';
        if (!strcmp(line, "Name")) copy_text(name, sizeof(name), eq);
        else if (!strcmp(line, "Exec")) copy_text(exec, sizeof(exec), eq);
        else if (!strcmp(line, "Hidden")) hidden = desktop_truthy(eq);
        else if (!strcmp(line, "NoDisplay")) nodisplay = desktop_truthy(eq);
        else if (!strcmp(line, "Type")) application = strcmp(eq, "Application") == 0;
    }
    fclose(f);
    if (!application || hidden || nodisplay || !name[0] || !exec[0]) return;

    struct launcher_app *app = &shell->apps[shell->app_count];
    copy_text(app->name, sizeof(app->name), name);
    sanitize_exec(app->exec, sizeof(app->exec), exec);
    if (!app->exec[0]) return;
    shell->app_count++;
}

static void scan_dir(struct shell *shell, const char *base) {
    if (!base || !*base) return;
    char path[4096];
    snprintf(path, sizeof(path), "%s/applications", base);
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && shell->app_count < MAX_APPS) {
        size_t len = strlen(entry->d_name);
        if (len < 9 || strcmp(entry->d_name + len - 8, ".desktop") != 0) continue;
        char file[4096];
        int written = snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(file)) continue;
        add_desktop_file(shell, file);
    }
    closedir(dir);
}

void apps_load(struct shell *shell) {
    const char *home = getenv("HOME");
    const char *xdg_home = getenv("XDG_DATA_HOME");
    char local[4096];
    if (xdg_home && *xdg_home) scan_dir(shell, xdg_home);
    else if (home && *home) {
        snprintf(local, sizeof(local), "%s/.local/share", home);
        scan_dir(shell, local);
    }

    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) dirs = "/usr/local/share:/usr/share";
    char *copy = strdup(dirs);
    if (copy) {
        char *save = NULL;
        for (char *p = strtok_r(copy, ":", &save); p; p = strtok_r(NULL, ":", &save))
            scan_dir(shell, p);
        free(copy);
    }
    fprintf(stderr, "shady-shell: indexed %zu applications\n", shell->app_count);
}

size_t apps_matching(struct shell *shell, const char *query, size_t *out, size_t max) {
    size_t count = 0;
    for (size_t i = 0; i < shell->app_count && count < max; i++) {
        if (ascii_contains_ci(shell->apps[i].name, query) ||
                ascii_contains_ci(shell->apps[i].exec, query)) {
            out[count++] = i;
        }
    }
    return count;
}

void apps_spawn(const char *command) {
    if (!command || !command[0]) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        /* Keep the running Wayland session's environment. A login shell
         * re-runs profile scripts and can replace PATH and display settings. */
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        perror("shady-shell: could not execute application shell");
        _exit(127);
    }
}
