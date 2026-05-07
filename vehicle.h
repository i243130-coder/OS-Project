/*
 * ============================================================================
 *  vehicle.h - Vehicle Thread Module Interface
 * ============================================================================
 *  Each vehicle is a pthread that simulates driving through intersections.
 *  Vehicles have categories, priorities, and may attempt to park.
 * ============================================================================
 */

#ifndef VEHICLE_H
#define VEHICLE_H

#include "common.h"

/*
 * Arguments passed to each vehicle thread.
 */
typedef struct {
    Vehicle         *vehicle;      /* Pointer to vehicle metadata        */
    SimulationState *sim;          /* Pointer to shared simulation state */
    int              pipe_to_ctrl[2]; /* Pipe to notify controllers      */
} VehicleArgs;

/*
 * Vehicle thread entry point.
 * Each vehicle thread follows this lifecycle:
 *   1. Spawn → Approach intersection
 *   2. (Optional) Attempt parking
 *   3. Wait for green light
 *   4. Cross intersection
 *   5. Exit
 */
void* vehicle_thread(void *arg);

/*
 * Initialize a vehicle with random attributes.
 */
void vehicle_init(Vehicle *v, int id);

/*
 * Check if two directions can cross simultaneously (non-conflicting).
 * Returns 1 if the movements are compatible, 0 otherwise.
 */
int movements_compatible(Direction d1, Turn t1, Direction d2, Turn t2);

#endif /* VEHICLE_H */
