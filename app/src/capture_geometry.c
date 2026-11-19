#include "capture_geometry.h"

#include <stdlib.h>
#include <string.h>

static inline uint16_t
even_down(uint32_t v) {
    return (uint16_t) (v & ~1u);
}

struct sc_capture_rect
sc_capture_geometry_fit(struct sc_size frame, struct sc_size display) {
    struct sc_capture_rect rect = {
        .x = 0,
        .y = 0,
        .width = even_down(frame.width),
        .height = even_down(frame.height),
    };

    if (!display.width || !display.height || !frame.width || !frame.height) {
        return rect;
    }

    uint32_t fw = frame.width;
    uint32_t fh = frame.height;
    uint32_t dw = display.width;
    uint32_t dh = display.height;

    uint32_t w;
    uint32_t h;
    if (dw * fh > fw * dh) {
        // display is "wider" than the frame: limited by the frame width
        w = fw;
        h = (fw * dh + dw / 2) / dw;
    } else {
        // limited by the frame height
        h = fh;
        w = (fh * dw + dh / 2) / dh;
    }

    rect.width = even_down(w);
    rect.height = even_down(h);
    rect.x = even_down((fw - rect.width) / 2);
    rect.y = even_down((fh - rect.height) / 2);
    return rect;
}

bool
sc_capture_geometry_clamp(struct sc_capture_rect *rect, struct sc_size frame) {
    uint32_t x = rect->x & ~1u;
    uint32_t y = rect->y & ~1u;
    if (x >= frame.width || y >= frame.height) {
        return false;
    }

    uint32_t w = rect->width;
    uint32_t h = rect->height;
    if (x + w > frame.width) {
        w = frame.width - x;
    }
    if (y + h > frame.height) {
        h = frame.height - y;
    }

    w &= ~1u;
    h &= ~1u;
    if (!w || !h) {
        return false;
    }

    rect->x = x;
    rect->y = y;
    rect->width = w;
    rect->height = h;
    return true;
}

static int32_t
map_coord(int32_t v, uint16_t from, uint16_t to) {
    if (!from || !to) {
        return 0;
    }
    // Map pixel centers: (v + 0.5) * to / from, rounded down
    int64_t r = ((int64_t) 2 * v + 1) * to / ((int64_t) 2 * from);
    if (r < 0) {
        r = 0;
    } else if (r >= to) {
        r = to - 1;
    }
    return (int32_t) r;
}

struct sc_point
sc_capture_geometry_map(struct sc_point point, struct sc_size content,
                        struct sc_size display) {
    struct sc_point result = {
        .x = map_coord(point.x, content.width, display.width),
        .y = map_coord(point.y, content.height, display.height),
    };
    return result;
}

static bool
parse_u16(const char *s, char **end, uint16_t *out) {
    char *e;
    long v = strtol(s, &e, 10);
    if (e == s || v < 0 || v > 0xFFFF) {
        return false;
    }
    *out = (uint16_t) v;
    *end = e;
    return true;
}

bool
sc_capture_geometry_parse_crop(const char *s, struct sc_capture_rect *rect) {
    uint16_t values[4];
    const char *p = s;
    for (int i = 0; i < 4; ++i) {
        char *end;
        if (!parse_u16(p, &end, &values[i])) {
            return false;
        }
        if (i < 3) {
            if (*end != ':') {
                return false;
            }
            p = end + 1;
        } else if (*end != '\0') {
            return false;
        }
    }

    if (!values[0] || !values[1]) {
        return false;
    }

    rect->width = values[0];
    rect->height = values[1];
    rect->x = values[2];
    rect->y = values[3];
    return true;
}

bool
sc_capture_geometry_parse_display(const char *output, uint32_t display_id,
                                  struct sc_size *size, unsigned *rotation) {
    const char *line = output;
    while (line && *line) {
        char *end;
        unsigned long id = strtoul(line, &end, 10);
        if (end != line) {
            unsigned long w = strtoul(end, &end, 10);
            unsigned long h = strtoul(end, &end, 10);
            unsigned long rot = strtoul(end, &end, 10);
            if (id == display_id && w && h && w <= 0xFFFF && h <= 0xFFFF
                    && rot < 4) {
                size->width = (uint16_t) w;
                size->height = (uint16_t) h;
                *rotation = (unsigned) rot;
                return true;
            }
        }

        line = strchr(line, '\n');
        if (line) {
            ++line;
        }
    }
    return false;
}
