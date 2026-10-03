#include "session/session_actions.h"
#include "util/log.h"
#include <stdlib.h>
#include <unistd.h>

struct session_actions {
    int dummy;
};

struct session_actions *session_actions_create(void)
{
    struct session_actions *sa = calloc(1, sizeof(*sa));
    if (!sa) return NULL;
    log_debug("session actions created");
    return sa;
}

void session_actions_destroy(struct session_actions *sa)
{
    if (!sa) return;
    free(sa);
}

static void run_cmd(const char *cmd)
{
    log_info("exec: %s", cmd);
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(1);
    }
}

void session_actions_poweroff(struct session_actions *sa)
{
    (void)sa;
    run_cmd("systemctl poweroff || loginctl poweroff");
}

void session_actions_reboot(struct session_actions *sa)
{
    (void)sa;
    run_cmd("systemctl reboot || loginctl reboot");
}

void session_actions_suspend(struct session_actions *sa)
{
    (void)sa;
    run_cmd("systemctl suspend || loginctl suspend");
}

void session_actions_logout(struct session_actions *sa)
{
    (void)sa;
    run_cmd("loginctl terminate-session $XDG_SESSION_ID || loginctl terminate-user $USER");
}
