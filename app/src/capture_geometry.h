#ifndef SC_CAPTURE_GEOMETRY_H
#define SC_CAPTURE_GEOMETRY_H

// Intentionally independent from common.h (no FFmpeg/SDL headers), so that it
// can be unit-tested anywhere.
#include <stdbool.h>
#include <stdint.h>

#include "coords.h"

/**
 * Pure geometry helpers for --capture (no SDL/FFmpeg dependency, unit-tested).
 *
 * The device outputs its display over HDMI, scaled to fit the capture frame
 * while keeping its aspect ratio, and centered (black borders around it).
 */

struct sc_capture_rect {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

/**
 * Compute the area of the capture frame showing the device display.
 *
 * All values are even (required to crop a YUV 4:2:0 frame). If display is
 * empty, the whole frame is returned.
 */
struct sc_capture_rect
sc_capture_geometry_fit(struct sc_size frame, struct sc_size display);

/**
 * Clamp a user-provided crop rectangle into the frame and make it even.
 *
 * Return false if the result is empty.
 */
bool
sc_capture_geometry_clamp(struct sc_capture_rect *rect, struct sc_size frame);

/**
 * Map a point expressed in content coordinates (the cropped frame, of size
 * content) to device display coordinates (of size display).
 *
 * The result is clamped to the display.
 */
struct sc_point
sc_capture_geometry_map(struct sc_point point, struct sc_size content,
                        struct sc_size display);

/**
 * Parse a crop rectangle "width:height:x:y".
 */
bool
sc_capture_geometry_parse_crop(const char *s, struct sc_capture_rect *rect);

/**
 * Parse one line "<display_id> <width> <height> <rotation>" (as produced by
 * the dumpsys command used by the capture display monitor) for the requested
 * display id.
 *
 * Return true if a line matching display_id was found.
 */
bool
sc_capture_geometry_parse_display(const char *output, uint32_t display_id,
                                  struct sc_size *size, unsigned *rotation);

#endif
