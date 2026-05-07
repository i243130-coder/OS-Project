/*
 * ============================================================================
 *  parking.h - Parking Lot Module Interface
 * ============================================================================
 *  Manages parking lots attached to intersections using dual semaphores:
 *  - spots_sem: tracks available parking spots (capacity: 10)
 *  - queue_sem: tracks available waiting queue slots (capacity: 5)
 * ============================================================================
 */

#ifndef PARKING_H
#define PARKING_H

#include "common.h"

/*
 * Initialize a parking lot's semaphores and mutexes.
 * Must be called before fork() since semaphores are in shared memory.
 */
void parking_init(ParkingLot *lot);

/*
 * Destroy parking lot resources (semaphores, mutex).
 */
void parking_destroy(ParkingLot *lot);

/*
 * Attempt to enter the parking queue (non-blocking).
 * Returns 1 if the vehicle got a queue slot, 0 if queue is full.
 * This is non-blocking to ensure vehicles never block the intersection.
 */
int parking_try_enter_queue(ParkingLot *lot, SimulationState *sim, IntersectionID iid, int vehicle_id);

/*
 * Wait for and occupy a parking spot (blocking within queue).
 * Called after successfully entering the queue.
 * Returns 1 on success, 0 if simulation is shutting down.
 */
int parking_wait_for_spot(ParkingLot *lot, SimulationState *sim, IntersectionID iid, int vehicle_id);

/*
 * Leave a parking spot and free resources.
 */
void parking_leave_spot(ParkingLot *lot, SimulationState *sim, IntersectionID iid, int vehicle_id);

/*
 * Leave the parking queue (called after getting a spot or giving up).
 */
void parking_leave_queue(ParkingLot *lot, SimulationState *sim, IntersectionID iid, int vehicle_id);

/*
 * Get current parking lot status.
 */
void parking_get_status(ParkingLot *lot, int *occupied, int *in_queue);

#endif /* PARKING_H */
