/* shady-auth SERVICE USER: check USER with the PAM service, for the shell's
 * lock screen. The password, if the service asks for one, is the first line
 * of stdin; a closed stdin answers no password (a fingerprint-only service
 * never asks). What PAM says goes to stdout, one line each:
 *   info <text> | error <text>   messages to show ("Place your finger…")
 *   ok | fail <reason>           the result, last
 * The exit status is 0 when authenticated. A separate process keeps PAM and
 * its modules (and their waits) away from the shell's own threads. */
#define _GNU_SOURCE
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char password[1024];
static int have_password = -1; /* not read yet */

static void say(const char *kind, const char *text) {
    printf("%s", kind);
    if (text) {
        putchar(' ');
        for (const char *p = text; *p; p++) putchar(*p == '\n' ? ' ' : *p);
    }
    putchar('\n');
    fflush(stdout);
}

static int read_password(void) {
    if (have_password < 0) {
        have_password = fgets(password, sizeof(password), stdin) != NULL;
        if (have_password) password[strcspn(password, "\n")] = '\0';
    }
    return have_password;
}

static int converse(int count, const struct pam_message **messages,
        struct pam_response **responses, void *data) {
    (void)data;
    struct pam_response *out = calloc((size_t)count, sizeof(*out));
    if (!out) return PAM_BUF_ERR;
    for (int i = 0; i < count; i++) {
        const struct pam_message *m = messages[i];
        switch (m->msg_style) {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
            if (!read_password()) {
                free(out);
                return PAM_CONV_ERR;
            }
            out[i].resp = strdup(password);
            if (!out[i].resp) {
                free(out);
                return PAM_BUF_ERR;
            }
            break;
        case PAM_ERROR_MSG:
            say("error", m->msg);
            break;
        case PAM_TEXT_INFO:
            say("info", m->msg);
            break;
        }
    }
    *responses = out;
    return PAM_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SERVICE USER\n", argv[0]);
        return 2;
    }
    const struct pam_conv conv = { converse, NULL };
    pam_handle_t *pam = NULL;
    int rc = pam_start(argv[1], argv[2], &conv, &pam);
    if (rc == PAM_SUCCESS) rc = pam_authenticate(pam, 0);
    if (rc == PAM_SUCCESS) rc = pam_acct_mgmt(pam, 0);
    const char *reason = pam_strerror(pam, rc);
    if (pam) pam_end(pam, rc);
    explicit_bzero(password, sizeof(password));
    if (rc == PAM_SUCCESS) {
        say("ok", NULL);
        return 0;
    }
    say("fail", reason);
    return 1;
}
