/*
 * ============================================================================
 *  display.h - Terminal Visualization Module Interface
 * ============================================================================
 *  Provides a rich terminal-based display of the simulation state using
 *  ANSI escape codes for colors, positioning, and Unicode characters.
 * ============================================================================
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "common.h"

/*
 * Display arguments for the display thread.
 */
typedef struct {
    SimulationState *sim;
    Vehicle         *vehicles;
    int              num_vehicles;
} DisplayArgs;

/*
 * Display thread function.
 * Continuously refreshes the terminal with simulation state.
 */
void* display_thread(void *arg);

/*
 * Print a one-time final summary when simulation ends.
 */
void display_final_summary(SimulationState *sim, Vehicle *vehicles, int n);

#endif /* DISPLAY_H */
