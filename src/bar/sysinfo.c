#include "bar/sysinfo.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

void sysinfo_init(struct sysinfo_state *s)
{
    memset(s, 0, sizeof(*s));
    s->cpu_pct = -1;
    s->ram_pct = -1;
    s->bat_pct = -1;
}

static int read_cpu(struct sysinfo_state *s)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    unsigned long long v[10] = { 0 };
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
    fclose(f);
    if (n < 4) return -1;
    /* guest/guest_nice are already included in user/nice */
    unsigned long long idle = v[3] + v[4];
    unsigned long long total = 0;
    for (int i = 0; i < 8 && i < n; i++) total += v[i];
    int pct = -1;
    if (s->prev_total && total > s->prev_total) {
        unsigned long long dt = total - s->prev_total;
        unsigned long long di = idle - s->prev_idle;
        pct = (int)((100ULL * (dt - di) + dt / 2) / dt);
    }
    s->prev_total = total;
    s->prev_idle = idle;
    return pct;
}

static int read_ram(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[128];
    unsigned long long total = 0, avail = 0;
    while (fgets(line, sizeof(line), f) && (!total || !avail)) {
        sscanf(line, "MemTotal: %llu kB", &total);
        sscanf(line, "MemAvailable: %llu kB", &avail);
    }
    fclose(f);
    if (!total) return -1;
    return (int)((100ULL * (total - avail) + total / 2) / total);
}

static void probe_battery(struct sysinfo_state *s)
{
    s->bat_probed = true;
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        char type[32] = { 0 };
        char p[300];
        snprintf(p, sizeof(p), "/sys/class/power_supply/%s/type", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        if (fgets(type, sizeof(type), f) && strncmp(type, "Battery", 7) == 0) {
            snprintf(s->bat_path, sizeof(s->bat_path), "/sys/class/power_supply/%s", e->d_name);
            fclose(f);
            break;
        }
        fclose(f);
    }
    closedir(d);
}

static void read_battery(struct sysinfo_state *s)
{
    if (!s->bat_probed) probe_battery(s);
    s->bat_pct = -1;
    if (!s->bat_path[0]) return;
    char p[300], buf[32];
    snprintf(p, sizeof(p), "%s/capacity", s->bat_path);
    FILE *f = fopen(p, "r");
    if (!f) return;
    int cap = -1;
    if (fscanf(f, "%d", &cap) != 1) cap = -1;
    fclose(f);
    s->bat_pct = cap;
    snprintf(p, sizeof(p), "%s/status", s->bat_path);
    f = fopen(p, "r");
    s->bat_charging = false;
    if (f) {
        if (fgets(buf, sizeof(buf), f)) s->bat_charging = strncmp(buf, "Charging", 8) == 0 || strncmp(buf, "Full", 4) == 0;
        fclose(f);
    }
}

bool sysinfo_update(struct sysinfo_state *s)
{
    int cpu = read_cpu(s), ram = read_ram();
    int old_bat = s->bat_pct;
    bool old_chg = s->bat_charging;
    read_battery(s);
    bool changed = cpu != s->cpu_pct || ram != s->ram_pct || old_bat != s->bat_pct || old_chg != s->bat_charging;
    s->cpu_pct = cpu;
    s->ram_pct = ram;
    return changed;
}
