#ifndef SCRCPY_H
#define SCRCPY_H

#include "common.h"

#include "options.h"

enum scrcpy_exit_code {
    // Normal program termination
    SCRCPY_EXIT_SUCCESS,

    // No connection could be established
    SCRCPY_EXIT_FAILURE,

    // Device was disconnected while running
    SCRCPY_EXIT_DISCONNECTED,

    // scrcpy-cam: the display to control changed (DeX switched on/off) while
    // it was chosen automatically; restart to follow it (see the launcher)
    SCRCPY_EXIT_DISPLAY_CHANGED,
};

enum scrcpy_exit_code
scrcpy(struct scrcpy_options *options);

#endif
