#include <assert.h>
#include <stdio.h>

#include "capture_geometry.h"

static void test_fit_portrait_in_landscape(void) {
    // S20 FE mirrored over HDMI: 1080x2400 in a 1920x1080 frame
    struct sc_size frame = {1920, 1080};
    struct sc_size display = {1080, 2400};
    struct sc_capture_rect r = sc_capture_geometry_fit(frame, display);
    assert(r.height == 1080);
    assert(r.width == 486);
    assert(r.x == 716);
    assert(r.y == 0);
    assert(r.x % 2 == 0 && r.width % 2 == 0);
}

static void test_fit_landscape_wider(void) {
    // S23+ rotated: 2340x1080 in 1920x1080
    struct sc_size frame = {1920, 1080};
    struct sc_size display = {2340, 1080};
    struct sc_capture_rect r = sc_capture_geometry_fit(frame, display);
    assert(r.width == 1920);
    assert(r.height == 886);
    assert(r.x == 0);
    assert(r.y == 96);
}

static void test_fit_same_ratio(void) {
    // DeX: 1920x1080 in 1920x1080
    struct sc_size frame = {1920, 1080};
    struct sc_size display = {1920, 1080};
    struct sc_capture_rect r = sc_capture_geometry_fit(frame, display);
    assert(r.x == 0 && r.y == 0 && r.width == 1920 && r.height == 1080);
}

static void test_fit_unknown_display(void) {
    struct sc_size frame = {1921, 1081};
    struct sc_size display = {0, 0};
    struct sc_capture_rect r = sc_capture_geometry_fit(frame, display);
    assert(r.x == 0 && r.y == 0 && r.width == 1920 && r.height == 1080);
}

static void test_clamp(void) {
    struct sc_size frame = {1920, 1080};
    struct sc_capture_rect r = {.x = 1801, .y = 3, .width = 500, .height = 2000};
    assert(sc_capture_geometry_clamp(&r, frame));
    assert(r.x == 1800 && r.y == 2 && r.width == 120 && r.height == 1078);

    struct sc_capture_rect out = {.x = 1920, .y = 0, .width = 10, .height = 10};
    assert(!sc_capture_geometry_clamp(&out, frame));
}

static void test_map(void) {
    struct sc_size content = {486, 1080};
    struct sc_size display = {1080, 2400};

    struct sc_point p = sc_capture_geometry_map((struct sc_point) {0, 0},
                                                content, display);
    assert(p.x == 1 && p.y == 1);

    p = sc_capture_geometry_map((struct sc_point) {485, 1079}, content, display);
    assert(p.x == 1078 && p.y == 2398);

    p = sc_capture_geometry_map((struct sc_point) {243, 540}, content, display);
    assert(p.x == 541 && p.y == 1201);

    // out of bounds input is clamped
    p = sc_capture_geometry_map((struct sc_point) {-50, 5000}, content, display);
    assert(p.x == 0 && p.y == 2399);

    // identity
    struct sc_size same = {1920, 1080};
    p = sc_capture_geometry_map((struct sc_point) {100, 200}, same, same);
    assert(p.x == 100 && p.y == 200);
}

static void test_parse_crop(void) {
    struct sc_capture_rect r;
    assert(sc_capture_geometry_parse_crop("486:1080:716:0", &r));
    assert(r.width == 486 && r.height == 1080 && r.x == 716 && r.y == 0);

    assert(!sc_capture_geometry_parse_crop("486:1080:716", &r));
    assert(!sc_capture_geometry_parse_crop("0:1080:0:0", &r));
    assert(!sc_capture_geometry_parse_crop("a:b:c:d", &r));
    assert(!sc_capture_geometry_parse_crop("1:2:3:4x", &r));
    assert(!sc_capture_geometry_parse_crop("70000:2:3:4", &r));
}

static void test_parse_display(void) {
    const char *out = "0 1080 2400 0\r\n2 1920 1080 0\r\n15 1920 1080 0\r\n";
    struct sc_size size;
    unsigned rot;

    assert(sc_capture_geometry_parse_display(out, 0, &size, &rot));
    assert(size.width == 1080 && size.height == 2400 && rot == 0);

    assert(sc_capture_geometry_parse_display(out, 15, &size, &rot));
    assert(size.width == 1920 && size.height == 1080);

    assert(!sc_capture_geometry_parse_display(out, 1, &size, &rot));
    assert(!sc_capture_geometry_parse_display("", 0, &size, &rot));

    const char *rotated = "0 2400 1080 1\n";
    assert(sc_capture_geometry_parse_display(rotated, 0, &size, &rot));
    assert(size.width == 2400 && size.height == 1080 && rot == 1);
}

static void test_select_display(void) {
    // only the built-in display
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\ndex 0\n") == 0);
    // mirror mode: the HDMI display is listed as EXTERNAL, but DeX is off
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\r\n9 1920 1080 0 EXTERNAL\r\ndex 0\r\n") == 0);
    // DeX on HDMI
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\r\n9 1920 1080 0 EXTERNAL\r\ndex 1\r\n") == 9);
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\n9 1920 1080 0 EXTERNAL\ndex 1") == 9);
    // other devices: no DeX setting
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\n9 1920 1080 0 EXTERNAL\ndex null\n") == 0);
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\n9 1920 1080 0 EXTERNAL\ndex 10\n") == 0);
    // the parser of the display size still accepts the extra token
    struct sc_size size;
    unsigned rot;
    assert(sc_capture_geometry_parse_display("9 1920 1080 0 EXTERNAL\n", 9,
                                             &size, &rot));
    assert(size.width == 1920 && size.height == 1080 && rot == 0);
    // ambiguous: two external displays -> built-in
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\n9 1920 1080 0 EXTERNAL\n"
        "11 1280 720 0 EXTERNAL\ndex 1\n") == 0);
    // virtual displays (screen recording, casting) are ignored
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0 INTERNAL\n5 720 1280 0 VIRTUAL\ndex 1\n") == 0);
    // older format without the type
    assert(sc_capture_geometry_select_display(
        "0 1080 2340 0\n2 1920 1080 0\ndex 1\n") == 0);
    // the "dex" line is ignored by the size parser
    assert(sc_capture_geometry_parse_display("dex 1\n0 1080 2340 0\n", 0,
                                             &size, &rot));
    assert(sc_capture_geometry_select_display("") == 0);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_fit_portrait_in_landscape();
    test_fit_landscape_wider();
    test_fit_same_ratio();
    test_fit_unknown_display();
    test_clamp();
    test_map();
    test_parse_crop();
    test_parse_display();
    test_select_display();
    printf("test_capture_geometry: OK\n");
    return 0;
}
