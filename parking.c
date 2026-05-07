/*
 * ============================================================================
 *  parking.c - Parking Lot Module Implementation
 * ============================================================================
 *  Implements the dual-semaphore parking lot system:
 *  - spots_sem (init=10): represents available parking spaces
 *  - queue_sem (init=5):  represents available waiting queue slots
 *
 *  Key Design Decisions:
 *  1. parking_try_enter_queue uses sem_trywait (non-blocking) so vehicles
 *     never block the intersection while trying to park.
 *  2. parking_wait_for_spot uses sem_wait (blocking) since the vehicle is
 *     already in the queue and off the intersection.
 *  3. All counter updates are protected by park_mutex.
 * ============================================================================
 */

#include "parking.h"

/* Forward declaration for logging */
extern void sim_log(SimulationState *sim, const char *fmt, ...);

/*
 * Initialize parking lot semaphores and mutex.
 * Uses unnamed semaphores with pshared=1 since they live in shared memory.
 */
void parking_init(ParkingLot *lot) {
    /* Initialize semaphore for parking spots (10 available) */
    if (sem_init(&lot->spots_sem, 1, PARKING_SPOTS) != 0) {
        perror("sem_init(spots_sem)");
        exit(EXIT_FAILURE);
    }

    /* Initialize semaphore for waiting queue slots (5 available) */
    if (sem_init(&lot->queue_sem, 1, PARKING_QUEUE_SIZE) != 0) {
        perror("sem_init(queue_sem)");
        exit(EXIT_FAILURE);
    }

    lot->occupied_spots   = 0;
    lot->vehicles_in_queue = 0;

    /* Initialize process-shared mutex for counter protection */
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&lot->park_mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);
}

/*
 * Clean up parking lot resources.
 */
void parking_destroy(ParkingLot *lot) {
    sem_destroy(&lot->spots_sem);
    sem_destroy(&lot->queue_sem);
    pthread_mutex_destroy(&lot->park_mutex);
}

/*
 * Try to enter the parking waiting queue (non-blocking).
 *
 * Uses sem_trywait on queue_sem:
 *   - If a queue slot is available, decrements the semaphore and returns 1.
 *   - If the queue is full, returns 0 immediately (no blocking).
 *
 * This ensures that vehicles at the intersection never block traffic
 * while trying to enter the parking queue.
 */
int parking_try_enter_queue(ParkingLot *lot, SimulationState *sim,
                            IntersectionID iid, int vehicle_id) {
    if (sem_trywait(&lot->queue_sem) == 0) {
        /* Successfully got a queue slot */
        pthread_mutex_lock(&lot->park_mutex);
        lot->vehicles_in_queue++;
        int in_q = lot->vehicles_in_queue;
        pthread_mutex_unlock(&lot->park_mutex);

        sim_log(sim, "%s Vehicle #%02d entered parking queue at %s (queue: %d/%d)",
                "🅿️ ", vehicle_id, intersection_str(iid), in_q, PARKING_QUEUE_SIZE);
        return 1;
    }

    /* Queue is full */
    sim_log(sim, "⛔ Vehicle #%02d: parking queue full at %s, skipping park",
            vehicle_id, intersection_str(iid));
    return 0;
}

/*
 * Wait for a parking spot (blocking).
 *
 * Called ONLY after successfully entering the queue. The vehicle is
 * logically off the intersection and in the parking queue, so blocking
 * here is safe and will not cause intersection gridlock.
 *
 * Uses sem_wait on spots_sem with periodic checks for shutdown.
 */
int parking_wait_for_spot(ParkingLot *lot, SimulationState *sim,
                          IntersectionID iid, int vehicle_id) {
    /* Use a timed wait so we can check for shutdown */
    while (sim->simulation_running && !g_shutdown_flag) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;  /* Check every second */

        int ret = sem_timedwait(&lot->spots_sem, &ts);
        if (ret == 0) {
            /* Got a parking spot! */
            pthread_mutex_lock(&lot->park_mutex);
            lot->occupied_spots++;
            int occ = lot->occupied_spots;
            pthread_mutex_unlock(&lot->park_mutex);

            sim_log(sim, "🅿️  Vehicle #%02d parked at %s (spots: %d/%d)",
                    vehicle_id, intersection_str(iid), occ, PARKING_SPOTS);
            return 1;
        }

        if (errno != ETIMEDOUT) {
            /* Actual error (e.g., signal interruption) */
            if (errno == EINTR) continue;
            break;
        }
    }

    /* Shutdown occurred while waiting */
    return 0;
}

/*
 * Leave a parking spot, releasing resources.
 * Increments spots_sem so another vehicle can park.
 */
void parking_leave_spot(ParkingLot *lot, SimulationState *sim,
                        IntersectionID iid, int vehicle_id) {
    pthread_mutex_lock(&lot->park_mutex);
    lot->occupied_spots--;
    int occ = lot->occupied_spots;
    pthread_mutex_unlock(&lot->park_mutex);

    sem_post(&lot->spots_sem);  /* Release parking spot */

    sim_log(sim, "🅿️  Vehicle #%02d left parking at %s (spots: %d/%d)",
            vehicle_id, intersection_str(iid), occ, PARKING_SPOTS);
}

/*
 * Leave the parking queue, releasing the queue slot.
 * Increments queue_sem so another vehicle can enter the queue.
 */
void parking_leave_queue(ParkingLot *lot, SimulationState *sim,
                         IntersectionID iid, int vehicle_id) {
    pthread_mutex_lock(&lot->park_mutex);
    lot->vehicles_in_queue--;
    pthread_mutex_unlock(&lot->park_mutex);

    sem_post(&lot->queue_sem);  /* Release queue slot */
}

/*
 * Get a snapshot of current parking lot occupancy.
 */
void parking_get_status(ParkingLot *lot, int *occupied, int *in_queue) {
    pthread_mutex_lock(&lot->park_mutex);
    *occupied = lot->occupied_spots;
    *in_queue = lot->vehicles_in_queue;
    pthread_mutex_unlock(&lot->park_mutex);
}
