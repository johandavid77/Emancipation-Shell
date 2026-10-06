#ifndef EMANCIPATION_SYSINFO_H
#define EMANCIPATION_SYSINFO_H

#include <stdbool.h>

struct sysinfo_state {
    /* /proc/stat previous sample */
    unsigned long long prev_total, prev_idle;
    int cpu_pct;      /* -1 until two samples exist */
    int ram_pct;      /* -1 if unavailable */
    int bat_pct;      /* -1 if no battery */
    bool bat_charging;
    char bat_path[256]; /* /sys/class/power_supply/BATx, discovered once */
    bool bat_probed;
};

void sysinfo_init(struct sysinfo_state *s);
/* Re-sample everything. Returns true if any displayed value changed. */
bool sysinfo_update(struct sysinfo_state *s);

#endif /* EMANCIPATION_SYSINFO_H */
