#ifndef EMANCIPATION_ICS_H
#define EMANCIPATION_ICS_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Local iCalendar reading, mirroring Noctalia's vdir path and collection layout
 * (calendar/vdir_reader.cpp: $XDG_DATA_HOME/calendars, leaf dirs with *.ics,
 * `displayname` / `color` / `order` side files). Parsing is hand rolled because
 * Noctalia uses libical and we do not link it.
 *
 * Supported: VEVENT with SUMMARY, DTSTART/DTEND, LOCATION, UID; VALUE=DATE
 * all-day events, UTC ("...Z") timestamps, line folding, escaped text.
 * Not supported: RRULE recurrence expansion, VTIMEZONE custom offsets (a
 * TZID timestamp is read as local time) and VALARM reminders. */

#define ICS_MAX_EVENTS 32

struct cal_event {
    char summary[192];
    char location[128];
    char uid[128];
    char collection[96];
    char color[16];
    time_t start;
    time_t end;
    bool all_day;
};

struct cal_day {
    struct cal_event ev[ICS_MAX_EVENTS];
    int n;
    int collections;
};

/* Loads every event overlapping the given local day. Returns the number of
 * events stored (capped at ICS_MAX_EVENTS). */
int ics_load_day(int year, int mon /* 1-12 */, int day, struct cal_day *out);

#endif /* EMANCIPATION_ICS_H */