#ifndef EMANCIPATION_SESSION_ACTIONS_H
#define EMANCIPATION_SESSION_ACTIONS_H

struct session_actions;

struct session_actions *session_actions_create(void);
void session_actions_destroy(struct session_actions *sa);
void session_actions_poweroff(struct session_actions *sa);
void session_actions_reboot(struct session_actions *sa);
void session_actions_suspend(struct session_actions *sa);
void session_actions_logout(struct session_actions *sa);

#endif /* EMANCIPATION_SESSION_ACTIONS_H */
