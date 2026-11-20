#ifndef SC_CAPTURE_H
#define SC_CAPTURE_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_camera.h>

#include "capture_geometry.h"
#include "coords.h"
#include "trait/frame_source.h"
#include "util/intr.h"
#include "util/thread.h"

/**
 * scrcpy-cam: frame source reading a local capture card (the device HDMI
 * output) through the SDL camera API.
 *
 * The black borders around the device display are cropped, so that the
 * produced frames contain exactly the device display. The current size of the
 * controlled display is polled through adb, and is used both to compute the
 * crop area and to map input events to device display coordinates (the server
 * runs without video, so it expects raw display coordinates).
 */
// Relation between the frames produced and the device display
struct sc_capture_mapping {
    struct sc_size content; // size of the produced frames (crop area)
    struct sc_size display; // display size used for the crop (0 if unknown)
};

struct sc_capture {
    struct sc_frame_source frame_source; // frame source trait

    const char *name_filter;
    uint16_t req_width;
    uint16_t req_height;
    uint16_t req_fps;
    bool has_fixed_crop;
    struct sc_capture_rect fixed_crop;

    const char *serial;
    uint32_t display_id;

    sc_thread thread;
    sc_thread monitor_thread;
    bool monitor_started;
    struct sc_intr intr;

    sc_mutex mutex;
    sc_cond cond;
    bool stopped; // protected by mutex
    SDL_CameraID camera_id; // protected by mutex, 0 once removed

    // current size of the controlled display (0 if unknown)
    struct sc_size display_size; // protected by mutex
    unsigned rotation; // protected by mutex, for logging

    // The two most recent mappings ([0] is the latest). Input events are
    // mapped according to the frame actually displayed, which may still be
    // the previous one just after a rotation.
    struct sc_capture_mapping mappings[2]; // protected by mutex
};

struct sc_capture_params {
    const char *name_filter;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    const char *crop; // "width:height:x:y", or NULL for auto
    const char *serial;
    uint32_t display_id;
};

bool
sc_capture_init(struct sc_capture *capture,
                const struct sc_capture_params *params);

bool
sc_capture_start(struct sc_capture *capture);

void
sc_capture_on_camera_removed(struct sc_capture *capture, SDL_CameraID id);

void
sc_capture_stop(struct sc_capture *capture);

void
sc_capture_join(struct sc_capture *capture);

void
sc_capture_destroy(struct sc_capture *capture);

/**
 * Position mapper for sc_controller_set_position_mapper() (userdata is the
 * struct sc_capture)
 *
 * Return false if the display size is not known yet (the event must then be
 * dropped: the server would inject it at raw, wrong coordinates).
 */
bool
sc_capture_map_position(struct sc_position *position, void *userdata);

#endif
