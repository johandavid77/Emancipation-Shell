#define _GNU_SOURCE
#include "calendar/ics.h"
#include "util/log.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_DEPTH 5
#define MAX_ICS_FILES 400

/* ---- text helpers -------------------------------------------------------- */

/* iCalendar lines are read with fgets and trimmed; long values are
 * truncated to the line buffer rather than unfolded (see ICS_MAX notes in
 * ics.h). */

/* unescape TEXT values: \\n \\, \; \\ */
static void ics_unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
                case 'n': case 'N': *w++ = ' '; break;
                case ',': case ';': *w++ = *r; break;
                default: *w++ = *r; break;
            }
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

/* Splits "NAME;PARAM=VAL:VALUE" -> value. Returns false when the line is not
 * the requested property. */
static bool prop_value(const char *line, const char *name, char *out, size_t outsz)
{
    size_t nlen = strlen(name);
    if (strncasecmp(line, name, nlen) != 0) return false;
    const char *p = line + nlen;
    if (*p != ';' && *p != ':') return false;
    /* skip parameters up to the value separator */
    const char *colon = strchr(p, ':');
    if (!colon) return false;
    p = colon + 1;
    size_t len = strlen(p);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    ics_unescape(out);
    return true;
}

static bool prop_param(const char *line, const char *name, const char *param)
{
    size_t nlen = strlen(name);
    if (strncasecmp(line, name, nlen) != 0) return false;
    const char *p = line + nlen;
    if (*p != ';') return false;
    return strstr(p, param) != NULL;
}

/* Parses DTSTART/DTEND values: YYYYMMDD, YYYYMMDDTHHMMSS[Z].
 * A TZID timestamp is read as local time (documented limitation). */
static bool parse_ical_time(const char *v, time_t *out, bool *all_day)
{
    while (*v == ' ') v++;
    if (strlen(v) < 8) return false;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    int y = atoi(v);
    int mo = (v[4] - '0') * 10 + (v[5] - '0');
    int d = (v[6] - '0') * 10 + (v[7] - '0');
    if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    if (all_day) *all_day = true;
    if (strlen(v) >= 15 && (v[8] == 'T' || v[8] == 't')) {
        tm.tm_hour = (v[9]-'0')*10 + (v[10]-'0');
        tm.tm_min = (v[11]-'0')*10 + (v[12]-'0');
        tm.tm_sec = (v[13]-'0')*10 + (v[14]-'0');
        tm.tm_isdst = -1;
        if (all_day) *all_day = false;
    }
    bool utc = (strlen(v) >= 16 && (v[15] == 'Z' || v[15] == 'z'));
    time_t t;
    if (utc) t = timegm(&tm);
    else t = mktime(&tm);
    if (t == (time_t)-1) return false;
    *out = t;
    return true;
}

/* ---- parsing ------------------------------------------------------------ */

static void parse_ics_file(const char *path, const char *coll, const char *color, time_t day_start,
                           time_t day_end, struct cal_day *out)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[4096];
    bool in_event = false, have_start = false;
    struct cal_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.end = ev.start = 0;
    ev.all_day = false;
    snprintf(ev.collection, sizeof(ev.collection), "%s", coll);
    snprintf(ev.color, sizeof(ev.color), "%s", color);

    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';
        if (l == 0) continue;
        if (strncmp(line, "BEGIN:VEVENT", 12) == 0) {
            in_event = true;
            memset(&ev, 0, sizeof(ev));
            have_start = false;
            continue;
        }
        if (strncmp(line, "END:VEVENT", 10) == 0) {
            if (in_event && have_start) {
                time_t e = ev.end ? ev.end : ev.start;
                /* overlap test: the day window is [day_start, day_end) */
                if (ev.start < day_end && e >= day_start) {
                    if (out->n < ICS_MAX_EVENTS) out->ev[out->n++] = ev;
                    else { fclose(f); return; }
                }
            }
            in_event = false;
            continue;
        }
        if (!in_event) continue;
        if (strncmp(line, "SUMMARY", 7) == 0) prop_value(line, "SUMMARY", ev.summary, sizeof(ev.summary));
        else if (strncmp(line, "LOCATION", 8) == 0) prop_value(line, "LOCATION", ev.location, sizeof(ev.location));
        else if (strncmp(line, "UID", 3) == 0) prop_value(line, "UID", ev.uid, sizeof(ev.uid));
        else if (strncmp(line, "DTSTART", 7) == 0) {
            if (prop_param(line, "DTSTART", "VALUE=DATE")) have_start = parse_ical_time(line, &ev.start, &ev.all_day);
            else have_start = parse_ical_time(line, &ev.start, &ev.all_day);
        } else if (strncmp(line, "DTEND", 5) == 0) {
            parse_ical_time(line, &ev.end, NULL);
        }
    }
    fclose(f);
}

static int g_files_seen;

static void scan_dir(const char *dir, const char *coll_name, const char *color, time_t ds,
                     time_t dend, struct cal_day *out, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    bool has_ics = false;
    while ((de = readdir(d)) != NULL) {
        const char *ext = strrchr(de->d_name, '.');
        if (ext && strcasecmp(ext, ".ics") == 0) { has_ics = true; break; }
    }
    rewinddir(d);

    char color_buf[16];
    if (!color || !*color) {
        color_buf[0] = '\0';
        char p[1024];
        snprintf(p, sizeof(p), "%s/color", dir);
        FILE *cf = fopen(p, "r");
        if (cf) {
            if (fgets(color_buf, sizeof(color_buf), cf)) {
                size_t l = strlen(color_buf);
                while (l && (color_buf[l-1]=='\n' || color_buf[l-1]=='\r' || color_buf[l-1]==' ')) color_buf[--l]='\0';
            }
            fclose(cf);
        }
    } else {
        snprintf(color_buf, sizeof(color_buf), "%s", color);
    }

    char name_buf[96];
    if (!coll_name || !*coll_name) {
        name_buf[0] = '\0';
        char p[1024];
        snprintf(p, sizeof(p), "%s/displayname", dir);
        FILE *nf = fopen(p, "r");
        if (nf) {
            if (fgets(name_buf, sizeof(name_buf), nf)) {
                size_t l = strlen(name_buf);
                while (l && (name_buf[l-1]=='\n' || name_buf[l-1]=='\r')) name_buf[--l]='\0';
            }
            fclose(nf);
        }
    } else {
        snprintf(name_buf, sizeof(name_buf), "%s", coll_name);
    }

    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < MAX_DEPTH && !has_ics) scan_dir(path, name_buf[0] ? name_buf : de->d_name,
                                                       color_buf[0] ? color_buf : NULL, ds, dend, out, depth + 1);
            continue;
        }
        const char *ext = strrchr(de->d_name, '.');
        if (!ext || strcasecmp(ext, ".ics") != 0) continue;
        if (++g_files_seen > MAX_ICS_FILES) { closedir(d); return; }
        parse_ics_file(path, name_buf, color_buf, ds, dend, out);
        out->collections++;
    }
    closedir(d);
}

int ics_load_day(int year, int mon, int day, struct cal_day *out)
{
    if (!out || mon < 1 || mon > 12 || day < 1 || day > 31) return 0;
    memset(out, 0, sizeof(*out));
    g_files_seen = 0;

    struct tm ds;
    memset(&ds, 0, sizeof(ds));
    ds.tm_year = year - 1900;
    ds.tm_mon = mon - 1;
    ds.tm_mday = day;
    ds.tm_isdst = -1;
    time_t start = mktime(&ds);
    ds.tm_mday = day + 1;
    time_t end = mktime(&ds);

    char root[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) snprintf(root, sizeof(root), "%s/calendars", xdg);
    else if (home) snprintf(root, sizeof(root), "%s/.local/share/calendars", home);
    else snprintf(root, sizeof(root), ".local/share/calendars");

    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    scan_dir(root, NULL, NULL, start, end, out, 0);
    /* stable order: by start time */
    for (int i = 0; i < out->n; i++) {
        for (int j = i + 1; j < out->n; j++) {
            if (out->ev[j].start < out->ev[i].start) {
                struct cal_event t = out->ev[i];
                out->ev[i] = out->ev[j];
                out->ev[j] = t;
            }
        }
    }
    return out->n;
}