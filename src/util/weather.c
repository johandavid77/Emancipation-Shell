#include "util/weather.h"
#include <stdio.h>

const char *weather_moon_glyph(double phase)
{
    if (phase < 0.02 || phase >= 0.98) return "\xF0\x9F\x8C\x91"; /* 🌑 */
    if (phase < 0.25) return "\xF0\x9F\x8C\x92"; /* 🌒 */
    if (phase < 0.27) return "\xF0\x9F\x8C\x93"; /* 🌓 */
    if (phase < 0.50) return "\xF0\x9F\x8C\x94"; /* 🌔 */
    if (phase < 0.52) return "\xF0\x9F\x8C\x95"; /* 🌕 */
    if (phase < 0.75) return "\xF0\x9F\x8C\x96"; /* 🌖 */
    if (phase < 0.77) return "\xF0\x9F\x8C\x97"; /* 🌗 */
    return "\xF0\x9F\x8C\x98"; /* 🌘 */
}

const char *weather_glyph(int code, int is_day, double moon_phase)
{
    if (!is_day) {
        switch (code) {
            case 0: case 1: return weather_moon_glyph(moon_phase);
            case 2: return "\xE2\x98\x81";        /* ☁  (no sun behind cloud) */
            case 51: case 53: case 55: case 56: case 57:
            case 80: case 81: case 82:
                return "\xF0\x9F\x8C\xA7";          /* 🌧  rain instead of 🌦 */
            default: break;
        }
    }
    switch (code) {
        case 0:  return "\xE2\x98\x80";             /* ☀ */
        case 1:  return "\xF0\x9F\x8C\xA4";         /* 🌤 */
        case 2:  return "\xE2\x9B\x85";             /* ⛅ */
        case 3:  return "\xE2\x98\x81";             /* ☁ */
        case 45: case 48: return "\xF0\x9F\x8C\xAB"; /* 🌫 */
        case 51: case 53: case 55: case 56: case 57: return "\xF0\x9F\x8C\xA6"; /* 🌦 */
        case 61: case 63: case 65: case 66: case 67: case 80: case 81: case 82: return "\xF0\x9F\x8C\xA7"; /* 🌧 */
        case 71: case 73: case 75: case 77: case 85: case 86: return "\xE2\x9D\x84"; /* ❄ */
        case 95: case 96: case 99: return "\xE2\x9B\x88"; /* ⛈ */
        default: return "\xE2\x98\x81";
    }
}

void weather_label_es(int code, char *out, size_t n)
{
    if (!out || n == 0) return;
    switch (code) {
        case 0:  snprintf(out, n, "Despejado"); break;
        case 1:  snprintf(out, n, "Mayormente despejado"); break;
        case 2:  snprintf(out, n, "Parcialmente nublado"); break;
        case 3:  snprintf(out, n, "Nublado"); break;
        case 45: case 48: snprintf(out, n, "Niebla"); break;
        case 51: case 53: case 55: snprintf(out, n, "Llovizna"); break;
        case 56: case 57: snprintf(out, n, "Llovizna helada"); break;
        case 61: case 63: case 65: snprintf(out, n, "Lluvia"); break;
        case 66: case 67: snprintf(out, n, "Lluvia helada"); break;
        case 71: case 73: case 75: snprintf(out, n, "Nieve"); break;
        case 77: snprintf(out, n, "Aguanieve"); break;
        case 80: case 81: case 82: snprintf(out, n, "Chubascos"); break;
        case 85: case 86: snprintf(out, n, "Chubascos de nieve"); break;
        case 95: snprintf(out, n, "Tormenta"); break;
        case 96: case 99: snprintf(out, n, "Tormenta con granizo"); break;
        default: snprintf(out, n, "Clima"); break;
    }
}
