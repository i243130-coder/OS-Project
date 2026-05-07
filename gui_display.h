/*
 * ============================================================================
 *  gui_display.h - SFML Graphical Display Interface
 * ============================================================================
 *  Provides a graphical visualization of the traffic simulation using SFML.
 *  Renders both intersections, traffic lights, vehicles, parking lots,
 *  and an event log in a polished 2D window.
 *
 *  This module is compiled as C++ (for SFML) but uses extern "C" to
 *  interface with the C simulation core.
 * ============================================================================
 */

#ifndef GUI_DISPLAY_H
#define GUI_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

/*
 * Arguments for the GUI display thread.
 */
typedef struct {
    SimulationState *sim;
    Vehicle         *vehicles;
    int              num_vehicles;
} GUIDisplayArgs;

/*
 * GUI display thread entry point.
 * Opens an SFML window and renders the simulation state in real-time.
 * Returns when the window is closed or simulation ends.
 */
void* gui_display_thread(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GUI_DISPLAY_H */
