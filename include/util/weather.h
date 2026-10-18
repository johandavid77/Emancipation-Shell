#ifndef EMANCIPATION_UTIL_WEATHER_H
#define EMANCIPATION_UTIL_WEATHER_H

#include <stddef.h>

/* Shared WMO weather-code presentation used by the bar widget and the control
 * center card. The glyph is night-aware: codes whose daytime emoji contains a
 * sun (partly cloudy, drizzle, showers) fall back to a moon/rain variant so the
 * widget never shows a sun after dark. */

/* Moon glyph for a phase in [0,1). */
const char *weather_moon_glyph(double phase);
/* Weather glyph for a WMO code, aware of day/night and the moon phase. */
const char *weather_glyph(int code, int is_day, double moon_phase);
/* Spanish label ("Despejado", "Lluvia", ...). Always NUL-terminated. */
void weather_label_es(int code, char *out, size_t n);

#endif /* EMANCIPATION_UTIL_WEATHER_H */
