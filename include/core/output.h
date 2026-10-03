#ifndef EMANCIPATION_OUTPUT_H
#define EMANCIPATION_OUTPUT_H

#include <wayland-client.h>
#include <stdint.h>

struct output {
    struct wl_output *wl;
    uint32_t global_name;
    int32_t x;
    int32_t y;
    int32_t w; /* logical width */
    int32_t h; /* logical height */
    int32_t scale;
    struct wl_list link;
    int ready;
};

void output_init(struct output *out, uint32_t global_name, struct wl_output *wl);
void output_destroy(struct output *out);

#endif /* EMANCIPATION_OUTPUT_H */
