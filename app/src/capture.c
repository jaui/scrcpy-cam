#include "capture.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <SDL3/SDL_camera.h>
#include <SDL3/SDL_surface.h>

#include "adb/adb.h"
#include "util/log.h"
#include "util/tick.h"

// Logical size and rotation of every display, one line per display:
//   "<display_id> <width> <height> <rotation>"
#define SC_CAPTURE_DISPLAY_CMD \
    "dumpsys display | grep mOverrideDisplayInfo | sed -E " \
    "'s/.*displayId ([0-9]+).* real ([0-9]+) x ([0-9]+).* rotation " \
    "([0-9]).*/\\1 \\2 \\3 \\4/'"

#define SC_CAPTURE_MONITOR_INTERVAL SC_TICK_FROM_MS(1000)
#define SC_CAPTURE_RETRY_INTERVAL SC_TICK_FROM_MS(1000)
#define SC_CAPTURE_POLL_INTERVAL SC_TICK_FROM_MS(2)
#define SC_CAPTURE_STALL_TIMEOUT SC_TICK_FROM_MS(3000)
#define SC_CAPTURE_PERMISSION_TIMEOUT SC_TICK_FROM_MS(10000)

static bool
contains_ignore_case(const char *haystack, const char *needle) {
    size_t n = strlen(needle);
    if (!n) {
        return true;
    }
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i]
                && tolower((unsigned char) p[i])
                    == tolower((unsigned char) needle[i])) {
            ++i;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

// Wait until the deadline or until the capture is stopped.
// Return false if stopped.
static bool
sc_capture_wait(struct sc_capture *capture, sc_tick deadline) {
    sc_mutex_lock(&capture->mutex);
    while (!capture->stopped) {
        bool timed_out =
            !sc_cond_timedwait(&capture->cond, &capture->mutex, deadline);
        if (timed_out) {
            break;
        }
    }
    bool stopped = capture->stopped;
    sc_mutex_unlock(&capture->mutex);
    return !stopped;
}

static bool
sc_capture_is_stopped(struct sc_capture *capture) {
    sc_mutex_lock(&capture->mutex);
    bool stopped = capture->stopped;
    sc_mutex_unlock(&capture->mutex);
    return stopped;
}

/* --- device display monitor (adb) --------------------------------------- */

static bool
sc_capture_poll_display(struct sc_capture *capture) {
    char buf[2048];
    ssize_t r = sc_adb_shell_read(&capture->intr, capture->serial,
                                  SC_CAPTURE_DISPLAY_CMD, buf, sizeof(buf),
                                  SC_ADB_NO_STDERR | SC_ADB_NO_LOGERR);
    if (r < 0) {
        return false;
    }

    struct sc_size size;
    unsigned rotation;
    bool found = sc_capture_geometry_parse_display(buf, capture->display_id,
                                                   &size, &rotation);
    if (!found) {
        return false;
    }

    sc_mutex_lock(&capture->mutex);
    bool changed = capture->display_size.width != size.width
                || capture->display_size.height != size.height
                || capture->rotation != rotation;
    capture->display_size = size;
    capture->rotation = rotation;
    sc_mutex_unlock(&capture->mutex);

    if (changed) {
        LOGI("Capture card: display %" PRIu32 " is %" PRIu16 "x%" PRIu16
             " (rotation %u)", capture->display_id, size.width, size.height,
             rotation * 90);
    }

    return true;
}

static int
run_monitor(void *data) {
    struct sc_capture *capture = data;

    bool warned = false;
    for (;;) {
        bool ok = sc_capture_poll_display(capture);
        if (!ok && !warned && !sc_capture_is_stopped(capture)) {
            LOGW("Capture card: could not read the size of display %" PRIu32
                 " (retrying)", capture->display_id);
            warned = true;
        } else if (ok) {
            warned = false;
        }

        sc_tick deadline = sc_tick_now() + SC_CAPTURE_MONITOR_INTERVAL;
        if (!sc_capture_wait(capture, deadline)) {
            break;
        }
    }

    LOGD("Capture card monitor thread ended");
    return 0;
}

/* --- capture ------------------------------------------------------------ */

static void
sc_capture_log_cameras(SDL_CameraID *ids, int count) {
    if (!count) {
        LOGW("Capture card: no camera device found");
        return;
    }
    LOGW("Capture card: available camera devices:");
    for (int i = 0; i < count; ++i) {
        const char *name = SDL_GetCameraName(ids[i]);
        LOGW("    - %s", name ? name : "(unknown)");
    }
}

static SDL_Camera *
sc_capture_open_camera(struct sc_capture *capture, bool log_errors) {
    int count = 0;
    SDL_CameraID *ids = SDL_GetCameras(&count);
    if (!ids) {
        if (log_errors) {
            LOGE("Capture card: could not list cameras: %s", SDL_GetError());
        }
        return NULL;
    }

    SDL_CameraID id = 0;
    const char *name = NULL;
    for (int i = 0; i < count; ++i) {
        const char *n = SDL_GetCameraName(ids[i]);
        if (n && contains_ignore_case(n, capture->name_filter)) {
            id = ids[i];
            name = n;
            break;
        }
    }

    if (!id) {
        if (log_errors) {
            LOGW("Capture card: no camera matching \"%s\"",
                 capture->name_filter);
            sc_capture_log_cameras(ids, count);
        }
        SDL_free(ids);
        return NULL;
    }
    SDL_free(ids);

    SDL_CameraSpec spec = {
        // Request the native format of UVC capture cards: with SDL 3.4 and
        // Media Foundation, requesting IYUV (converted by SDL) produced frames
        // full of zeros. NV12 is converted to YUV420P while cropping.
        .format = SDL_PIXELFORMAT_NV12,
        .colorspace = SDL_COLORSPACE_UNKNOWN,
        .width = capture->req_width,
        .height = capture->req_height,
        .framerate_numerator = capture->req_fps,
        .framerate_denominator = 1,
    };

    SDL_Camera *camera = SDL_OpenCamera(id, &spec);
    if (!camera) {
        if (log_errors) {
            LOGE("Capture card: could not open \"%s\": %s", name,
                 SDL_GetError());
        }
        return NULL;
    }

    sc_tick deadline = sc_tick_now() + SC_CAPTURE_PERMISSION_TIMEOUT;
    for (;;) {
        SDL_CameraPermissionState state = SDL_GetCameraPermissionState(camera);
        if (state == SDL_CAMERA_PERMISSION_STATE_APPROVED) {
            break;
        }
        if (state == SDL_CAMERA_PERMISSION_STATE_DENIED) {
            LOGE("Capture card: access to \"%s\" denied", name);
            SDL_CloseCamera(camera);
            return NULL;
        }
        if (sc_tick_now() >= deadline) {
            LOGE("Capture card: no permission to use \"%s\"", name);
            SDL_CloseCamera(camera);
            return NULL;
        }
        if (!sc_capture_wait(capture,
                             sc_tick_now() + SC_TICK_FROM_MS(10))) {
            SDL_CloseCamera(camera);
            return NULL;
        }
    }

    SDL_CameraSpec actual;
    if (SDL_GetCameraFormat(camera, &actual)) {
        float fps = actual.framerate_denominator
                  ? (float) actual.framerate_numerator
                        / actual.framerate_denominator
                  : 0;
        LOGI("Capture card: \"%s\" opened, %dx%d @ %.0f fps (%s)", name,
             actual.width, actual.height, fps,
             SDL_GetPixelFormatName(actual.format));
    } else {
        LOGI("Capture card: \"%s\" opened", name);
    }

    return camera;
}

static void
sc_capture_set_color(AVFrame *frame, SDL_Colorspace cs) {
    if (cs == SDL_COLORSPACE_UNKNOWN) {
        // HDMI capture cards usually produce limited range BT.709
        frame->colorspace = AVCOL_SPC_BT709;
        frame->color_range = AVCOL_RANGE_MPEG;
        return;
    }

    SDL_MatrixCoefficients matrix = SDL_COLORSPACEMATRIX(cs);
    switch (matrix) {
        case SDL_MATRIX_COEFFICIENTS_BT601:
        case SDL_MATRIX_COEFFICIENTS_BT470BG:
            frame->colorspace = AVCOL_SPC_SMPTE170M;
            break;
        case SDL_MATRIX_COEFFICIENTS_BT2020_NCL:
            frame->colorspace = AVCOL_SPC_BT2020_NCL;
            break;
        default:
            frame->colorspace = AVCOL_SPC_BT709;
            break;
    }

    frame->color_range = SDL_COLORSPACERANGE(cs) == SDL_COLOR_RANGE_FULL
                       ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
}

static struct sc_capture_rect
sc_capture_get_crop(struct sc_capture *capture, struct sc_size frame_size) {
    struct sc_capture_rect rect;
    if (capture->has_fixed_crop) {
        rect = capture->fixed_crop;
        if (!sc_capture_geometry_clamp(&rect, frame_size)) {
            rect = sc_capture_geometry_fit(frame_size, (struct sc_size) {0, 0});
        }
        return rect;
    }

    sc_mutex_lock(&capture->mutex);
    struct sc_size display = capture->display_size;
    sc_mutex_unlock(&capture->mutex);

    return sc_capture_geometry_fit(frame_size, display);
}

// Copy the crop area of a NV12/NV21/IYUV/YV12 surface into a new YUV420P frame
static AVFrame *
sc_capture_convert(struct sc_capture *capture, SDL_Surface *surface,
                   SDL_Colorspace colorspace) {
    SDL_PixelFormat fmt = surface->format;
    bool semi_planar = fmt == SDL_PIXELFORMAT_NV12
                    || fmt == SDL_PIXELFORMAT_NV21;
    bool planar = fmt == SDL_PIXELFORMAT_IYUV || fmt == SDL_PIXELFORMAT_YV12;
    if (!semi_planar && !planar) {
        static bool logged = false;
        if (!logged) {
            LOGE("Capture card: unsupported frame format %s",
                 SDL_GetPixelFormatName(fmt));
            logged = true;
        }
        return NULL;
    }

    if (surface->w < 2 || surface->h < 2 || surface->w > 0xFFFF
            || surface->h > 0xFFFF) {
        return NULL;
    }

    struct sc_size frame_size = {surface->w, surface->h};
    struct sc_capture_rect crop = sc_capture_get_crop(capture, frame_size);
    assert(crop.width && crop.height);
    assert(!(crop.x & 1) && !(crop.y & 1));
    assert(!(crop.width & 1) && !(crop.height & 1));

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        LOG_OOM();
        return NULL;
    }

    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = crop.width;
    frame->height = crop.height;
    if (av_frame_get_buffer(frame, 0) < 0) {
        LOG_OOM();
        av_frame_free(&frame);
        return NULL;
    }

    // Layout of SDL YUV surfaces (see SDL_CalculateYUVSize()): the Y plane
    // (pitch * h), followed by the chroma plane(s)
    int pitch = surface->pitch;
    const uint8_t *y_plane = surface->pixels;
    const uint8_t *chroma = y_plane + (size_t) pitch * surface->h;

    for (unsigned row = 0; row < crop.height; ++row) {
        memcpy(frame->data[0] + (size_t) row * frame->linesize[0],
               y_plane + (size_t) (crop.y + row) * pitch + crop.x,
               crop.width);
    }

    unsigned cw = crop.width / 2;
    unsigned ch = crop.height / 2;
    unsigned cx = crop.x / 2;
    unsigned cy = crop.y / 2;

    if (semi_planar) {
        // One interleaved plane (UVUV... for NV12, VUVU... for NV21), with the
        // same pitch as the Y plane
        bool nv21 = fmt == SDL_PIXELFORMAT_NV21;
        uint8_t *dst_u = frame->data[nv21 ? 2 : 1];
        uint8_t *dst_v = frame->data[nv21 ? 1 : 2];
        int dst_u_pitch = frame->linesize[nv21 ? 2 : 1];
        int dst_v_pitch = frame->linesize[nv21 ? 1 : 2];
        for (unsigned row = 0; row < ch; ++row) {
            const uint8_t *src = chroma + (size_t) (cy + row) * pitch + 2 * cx;
            uint8_t *u = dst_u + (size_t) row * dst_u_pitch;
            uint8_t *v = dst_v + (size_t) row * dst_v_pitch;
            for (unsigned i = 0; i < cw; ++i) {
                u[i] = src[2 * i];
                v[i] = src[2 * i + 1];
            }
        }
    } else {
        int uv_pitch = (pitch + 1) / 2;
        const uint8_t *p1 = chroma;
        const uint8_t *p2 = p1 + (size_t) uv_pitch * ((surface->h + 1) / 2);
        bool yv12 = fmt == SDL_PIXELFORMAT_YV12;
        const uint8_t *u_plane = yv12 ? p2 : p1;
        const uint8_t *v_plane = yv12 ? p1 : p2;
        for (unsigned row = 0; row < ch; ++row) {
            memcpy(frame->data[1] + (size_t) row * frame->linesize[1],
                   u_plane + (size_t) (cy + row) * uv_pitch + cx, cw);
            memcpy(frame->data[2] + (size_t) row * frame->linesize[2],
                   v_plane + (size_t) (cy + row) * uv_pitch + cx, cw);
        }
    }

    sc_capture_set_color(frame, colorspace);
    return frame;
}

static int
run_capture(void *data) {
    struct sc_capture *capture = data;

    AVCodecContext *ctx = NULL;
    bool sinks_open = false;
    struct sc_size last_size = {0, 0};
    SDL_Camera *camera = NULL;
    SDL_Colorspace colorspace = SDL_COLORSPACE_UNKNOWN;
    sc_tick last_frame = 0;
    bool log_open_errors = true;

    while (!sc_capture_is_stopped(capture)) {
        if (!camera) {
            camera = sc_capture_open_camera(capture, log_open_errors);
            if (!camera) {
                // Log only once until the camera is found
                log_open_errors = false;
                sc_tick deadline = sc_tick_now() + SC_CAPTURE_RETRY_INTERVAL;
                if (!sc_capture_wait(capture, deadline)) {
                    break;
                }
                continue;
            }
            log_open_errors = true;
            last_frame = sc_tick_now();

            SDL_CameraSpec spec;
            colorspace = SDL_GetCameraFormat(camera, &spec)
                       ? spec.colorspace : SDL_COLORSPACE_UNKNOWN;
        }

        Uint64 timestamp;
        SDL_Surface *surface = SDL_AcquireCameraFrame(camera, &timestamp);
        if (!surface) {
            if (sc_tick_now() - last_frame > SC_CAPTURE_STALL_TIMEOUT) {
                LOGW("Capture card: no frame received, reopening");
                SDL_CloseCamera(camera);
                camera = NULL;
                continue;
            }
            sc_tick deadline = sc_tick_now() + SC_CAPTURE_POLL_INTERVAL;
            if (!sc_capture_wait(capture, deadline)) {
                break;
            }
            continue;
        }

        last_frame = sc_tick_now();
        AVFrame *frame = sc_capture_convert(capture, surface, colorspace);
        SDL_ReleaseCameraFrame(camera, surface);
        if (!frame) {
            continue;
        }

        struct sc_size size = {frame->width, frame->height};
        if (!sinks_open) {
            ctx = avcodec_alloc_context3(NULL);
            if (!ctx) {
                LOG_OOM();
                av_frame_free(&frame);
                break;
            }
            ctx->codec_type = AVMEDIA_TYPE_VIDEO;
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            ctx->width = size.width;
            ctx->height = size.height;

            struct sc_stream_session session = {
                .video = {
                    .width = size.width,
                    .height = size.height,
                    .client_resized = false,
                },
            };
            if (!sc_frame_source_sinks_open(&capture->frame_source, ctx,
                                            &session)) {
                av_frame_free(&frame);
                break;
            }
            sinks_open = true;
            last_size = size;
        } else if (size.width != last_size.width
                || size.height != last_size.height) {
            LOGI("Capture card: content size %" PRIu16 "x%" PRIu16,
                 size.width, size.height);
            struct sc_stream_session session = {
                .video = {
                    .width = size.width,
                    .height = size.height,
                    .client_resized = false,
                },
            };
            enum sc_sink_result res =
                sc_frame_source_sinks_push_session(&capture->frame_source,
                                                   &session);
            if (res != SC_SINK_OK) {
                av_frame_free(&frame);
                break;
            }
            last_size = size;
        }

        enum sc_sink_result res =
            sc_frame_source_sinks_push(&capture->frame_source, frame);
        av_frame_free(&frame);
        if (res != SC_SINK_OK) {
            break;
        }
    }

    if (camera) {
        SDL_CloseCamera(camera);
    }

    if (sinks_open) {
        sc_frame_source_sinks_close(&capture->frame_source);
    }
    avcodec_free_context(&ctx);

    LOGD("Capture card thread ended");
    return 0;
}

bool
sc_capture_init(struct sc_capture *capture,
                const struct sc_capture_params *params) {
    assert(params->name_filter);
    assert(params->serial);

    capture->has_fixed_crop = false;
    if (params->crop) {
        if (!sc_capture_geometry_parse_crop(params->crop,
                                            &capture->fixed_crop)) {
            LOGE("Invalid capture card crop: %s", params->crop);
            return false;
        }
        capture->has_fixed_crop = true;
    }

    if (!sc_mutex_init(&capture->mutex)) {
        return false;
    }

    if (!sc_cond_init(&capture->cond)) {
        sc_mutex_destroy(&capture->mutex);
        return false;
    }

    if (!sc_intr_init(&capture->intr)) {
        sc_cond_destroy(&capture->cond);
        sc_mutex_destroy(&capture->mutex);
        return false;
    }

    sc_frame_source_init(&capture->frame_source);

    capture->name_filter = params->name_filter;
    capture->req_width = params->width;
    capture->req_height = params->height;
    capture->req_fps = params->fps;
    capture->serial = params->serial;
    capture->display_id = params->display_id;
    capture->stopped = false;
    capture->monitor_started = false;
    capture->display_size.width = 0;
    capture->display_size.height = 0;
    capture->rotation = 0;

    return true;
}

bool
sc_capture_start(struct sc_capture *capture) {
    // Read the display size once synchronously, so that the first frames are
    // already cropped correctly
    if (!sc_capture_poll_display(capture)) {
        LOGW("Capture card: could not read the size of display %" PRIu32
             ", the whole capture frame is used until it is known",
             capture->display_id);
    }

    bool ok = sc_thread_create(&capture->monitor_thread, run_monitor,
                               "scrcpy-capmon", capture);
    if (!ok) {
        LOGE("Could not start capture card monitor thread");
        return false;
    }
    capture->monitor_started = true;

    ok = sc_thread_create(&capture->thread, run_capture, "scrcpy-capture",
                          capture);
    if (!ok) {
        LOGE("Could not start capture card thread");
        sc_capture_stop(capture);
        sc_thread_join(&capture->monitor_thread, NULL);
        capture->monitor_started = false;
        return false;
    }

    return true;
}

void
sc_capture_stop(struct sc_capture *capture) {
    sc_mutex_lock(&capture->mutex);
    capture->stopped = true;
    sc_cond_broadcast(&capture->cond);
    sc_mutex_unlock(&capture->mutex);

    // Interrupt a pending adb command
    sc_intr_interrupt(&capture->intr);
}

void
sc_capture_join(struct sc_capture *capture) {
    sc_thread_join(&capture->thread, NULL);
    if (capture->monitor_started) {
        sc_thread_join(&capture->monitor_thread, NULL);
    }
}

void
sc_capture_destroy(struct sc_capture *capture) {
    sc_intr_destroy(&capture->intr);
    sc_cond_destroy(&capture->cond);
    sc_mutex_destroy(&capture->mutex);
}

void
sc_capture_map_position(struct sc_position *position, void *userdata) {
    struct sc_capture *capture = userdata;

    sc_mutex_lock(&capture->mutex);
    struct sc_size display = capture->display_size;
    sc_mutex_unlock(&capture->mutex);

    if (!display.width || !display.height) {
        // Unknown display size: assume the frame matches the display
        return;
    }

    position->point = sc_capture_geometry_map(position->point,
                                              position->screen_size, display);
    position->screen_size = display;
}
