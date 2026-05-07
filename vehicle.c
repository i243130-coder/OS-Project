/*
 * ============================================================================
 *  vehicle.c - Vehicle Thread Module Implementation
 * ============================================================================
 *  Implements the lifecycle of each vehicle thread:
 *    1. Spawn at random entry point with random attributes
 *    2. Approach the intersection
 *    3. Optionally attempt to park (non-emergency vehicles only)
 *    4. Wait for the traffic light to turn green for its direction
 *    5. Cross the intersection (emergency vehicles trigger preemption)
 *    6. Optionally travel to the other intersection
 *    7. Exit
 *
 *  Priority handling:
 *    - Emergency vehicles (Ambulance, Firetruck) trigger emergency preemption
 *    - Buses get medium priority (next in line after emergency)
 *    - Cars, Bikes, Tractors wait normally
 * ============================================================================
 */

#include "vehicle.h"
#include "parking.h"

/* External logging function from main.c */
extern void sim_log(SimulationState *sim, const char *fmt, ...);

/*
 * Check if two movements are non-conflicting.
 * Non-conflicting means they can safely cross at the same time.
 *
 * Compatible pairs:
 *   - N straight + S straight (opposing through traffic)
 *   - E straight + W straight (opposing through traffic)
 *   - N right + S right, E right + W right
 *   - Same direction movements are always compatible
 *
 * Conflicting:
 *   - N/S vs E/W directions (perpendicular)
 *   - Left turns conflict with most other movements
 */
int movements_compatible(Direction d1, Turn t1, Direction d2, Turn t2) {
    /* Same direction is always compatible */
    if (d1 == d2) return 1;

    /* North-South pair (opposing through traffic) */
    if ((d1 == NORTH && d2 == SOUTH) || (d1 == SOUTH && d2 == NORTH)) {
        /* Straight + Straight: compatible */
        if (t1 == GO_STRAIGHT && t2 == GO_STRAIGHT) return 1;
        /* Right + Right: compatible */
        if (t1 == GO_RIGHT && t2 == GO_RIGHT) return 1;
        /* Straight + Right: compatible */
        if ((t1 == GO_STRAIGHT && t2 == GO_RIGHT) ||
            (t1 == GO_RIGHT && t2 == GO_STRAIGHT)) return 1;
        /* Left turns conflict with opposing straight */
        return 0;
    }

    /* East-West pair (opposing through traffic) */
    if ((d1 == EAST && d2 == WEST) || (d1 == WEST && d2 == EAST)) {
        if (t1 == GO_STRAIGHT && t2 == GO_STRAIGHT) return 1;
        if (t1 == GO_RIGHT && t2 == GO_RIGHT) return 1;
        if ((t1 == GO_STRAIGHT && t2 == GO_RIGHT) ||
            (t1 == GO_RIGHT && t2 == GO_STRAIGHT)) return 1;
        return 0;
    }

    /* Perpendicular directions: always conflicting */
    return 0;
}

/*
 * Initialize a vehicle with random attributes.
 * Uses the vehicle's ID as part of the random seed for variety.
 */
void vehicle_init(Vehicle *v, int id) {
    v->id = id;

    /* Random vehicle type distribution:
     * ~10% Ambulance, ~10% Firetruck, ~15% Bus, ~35% Car, ~20% Bike, ~10% Tractor
     */
    int r = rand() % 100;
    if (r < 10)       v->type = AMBULANCE;
    else if (r < 20)  v->type = FIRETRUCK;
    else if (r < 35)  v->type = BUS;
    else if (r < 70)  v->type = CAR;
    else if (r < 90)  v->type = BIKE;
    else               v->type = TRACTOR;

    /* Random intersection */
    v->origin_intersection = (rand() % 2 == 0) ? F10 : F11;

    /* Random approach direction */
    v->origin_direction = (Direction)(rand() % NUM_DIRECTIONS);

    /* Random turn */
    v->destination = (Turn)(rand() % NUM_TURNS);

    /* Priority based on type */
    v->priority = get_vehicle_priority(v->type);

    /* Parking: only non-emergency vehicles may want to park (40% chance) */
    if (v->priority == PRIORITY_HIGH) {
        v->wants_parking = 0;  /* Emergency vehicles never park */
    } else {
        v->wants_parking = (rand() % 100 < 40) ? 1 : 0;
    }

    v->arrival_time = time(NULL);
    v->state = VSTATE_SPAWNED;
    v->active = 1;
}

/*
 * Request emergency preemption at an intersection.
 * Sets the emergency flag and direction so the controller gives priority.
 */
static void request_emergency_preemption(SimulationState *sim,
                                         IntersectionID iid,
                                         Vehicle *v) {
    IntersectionState *inter = &sim->intersections[iid];

    pthread_mutex_lock(&inter->crossing_mutex);
    inter->emergency_active = 1;
    inter->emergency_direction = v->origin_direction;
    inter->emergency_vehicle_type = v->type;
    inter->emergency_vehicle_id = v->id;
    inter->emergency_preemptions++;
    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);

    sim_log(sim, "🚨 %s %s #%02d EMERGENCY PREEMPTION at %s-%s!",
            vehicle_type_icon(v->type), vehicle_type_str(v->type),
            v->id, intersection_str(iid), direction_str(v->origin_direction));
}

/*
 * Clear emergency preemption after vehicle has crossed.
 */
static void clear_emergency_preemption(SimulationState *sim,
                                       IntersectionID iid,
                                       Vehicle *v) {
    IntersectionState *inter = &sim->intersections[iid];

    pthread_mutex_lock(&inter->crossing_mutex);
    if (inter->emergency_vehicle_id == v->id) {
        inter->emergency_active = 0;
        inter->emergency_direction = 0;
        inter->emergency_vehicle_id = 0;
        pthread_cond_broadcast(&inter->crossing_cond);
    }
    pthread_mutex_unlock(&inter->crossing_mutex);

    sim_log(sim, "✅ %s %s #%02d emergency cleared at %s",
            vehicle_type_icon(v->type), vehicle_type_str(v->type),
            v->id, intersection_str(iid));
}

/*
 * Wait for the traffic light to be green for this vehicle's direction.
 * Emergency vehicles trigger preemption instead of waiting.
 * Returns 1 when ready to cross, 0 if shutdown.
 */
static int wait_for_green(SimulationState *sim, Vehicle *v) {
    IntersectionState *inter = &sim->intersections[v->origin_intersection];

    /* Emergency vehicles request preemption */
    if (v->priority == PRIORITY_HIGH) {
        request_emergency_preemption(sim, v->origin_intersection, v);
        /* Brief pause to let controller process the preemption */
        usleep(500000);  /* 500ms */
        return 1;
    }

    v->state = VSTATE_WAITING;

    pthread_mutex_lock(&inter->crossing_mutex);

    while (sim->simulation_running && !g_shutdown_flag) {
        /* Check if our direction has a green light */
        if (inter->lights[v->origin_direction] == GREEN) {
            /* Also check no emergency is active that conflicts */
            if (!inter->emergency_active ||
                inter->emergency_direction == v->origin_direction) {
                break;  /* We can go! */
            }
        }

        /* Wait on condition variable with timeout */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_cond_timedwait(&inter->crossing_cond, &inter->crossing_mutex, &ts);
    }

    pthread_mutex_unlock(&inter->crossing_mutex);

    if (!sim->simulation_running || g_shutdown_flag) return 0;
    return 1;
}

/*
 * Cross the intersection.
 * Simulates the time taken to cross and updates state.
 */
static void cross_intersection(SimulationState *sim, Vehicle *v) {
    IntersectionState *inter = &sim->intersections[v->origin_intersection];

    v->state = VSTATE_CROSSING;

    pthread_mutex_lock(&inter->crossing_mutex);
    inter->vehicles_crossing++;
    pthread_mutex_unlock(&inter->crossing_mutex);

    sim_log(sim, "%s %s #%02d crossing %s (%s → %s)",
            vehicle_type_icon(v->type), vehicle_type_str(v->type),
            v->id, intersection_str(v->origin_intersection),
            direction_str(v->origin_direction), turn_str(v->destination));

    /* Simulate crossing time (emergency vehicles cross faster) */
    int cross_time = (v->priority == PRIORITY_HIGH) ? 1 : CROSSING_TIME_SEC;
    sleep(cross_time);

    pthread_mutex_lock(&inter->crossing_mutex);
    inter->vehicles_crossing--;
    inter->total_crossed++;
    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);

    v->state = VSTATE_CROSSED;

    sim_log(sim, "✅ %s #%02d crossed %s successfully",
            vehicle_type_icon(v->type), v->id,
            intersection_str(v->origin_intersection));
}

/*
 * Handle parking for a vehicle at its intersection.
 * Returns 1 if the vehicle parked and unparked successfully,
 * 0 if parking was skipped or failed.
 */
static int handle_parking(SimulationState *sim, Vehicle *v) {
    ParkingLot *lot = &sim->parking_lots[v->origin_intersection];

    /* Step 1: Try to enter the parking queue (non-blocking) */
    if (!parking_try_enter_queue(lot, sim, v->origin_intersection, v->id)) {
        v->wants_parking = 0;  /* Give up on parking */
        return 0;
    }

    v->state = VSTATE_QUEUE_PARK;

    /* Step 2: Wait for a parking spot (blocking, but off the intersection) */
    if (!parking_wait_for_spot(lot, sim, v->origin_intersection, v->id)) {
        parking_leave_queue(lot, sim, v->origin_intersection, v->id);
        return 0;
    }

    /* Step 3: Leave the queue slot (we now have a spot) */
    parking_leave_queue(lot, sim, v->origin_intersection, v->id);

    v->state = VSTATE_PARKED;

    /* Step 4: Simulate parking duration */
    int park_time = PARK_DURATION_MIN +
                    (rand() % (PARK_DURATION_MAX - PARK_DURATION_MIN + 1));
    sim_log(sim, "🅿️  %s #%02d parked for %d seconds at %s",
            vehicle_type_icon(v->type), v->id, park_time,
            intersection_str(v->origin_intersection));
    sleep(park_time);

    /* Step 5: Leave the parking spot */
    parking_leave_spot(lot, sim, v->origin_intersection, v->id);

    return 1;
}

/*
 * Vehicle thread main function.
 *
 * Full lifecycle:
 *   1. Spawn and approach
 *   2. If wants_parking → try to park
 *   3. Wait for green light
 *   4. Cross intersection
 *   5. If destination leads to other intersection → travel and cross again
 *   6. Exit
 */
void* vehicle_thread(void *arg) {
    VehicleArgs *vargs = (VehicleArgs *)arg;
    Vehicle *v = vargs->vehicle;
    SimulationState *sim = vargs->sim;

    /* Record arrival time */
    v->arrival_time = time(NULL);
    v->state = VSTATE_APPROACHING;

    sim_log(sim, "%s %s %s #%02d spawned at %s-%s (→ %s, park=%s)",
            priority_color(v->priority),
            vehicle_type_icon(v->type), vehicle_type_str(v->type),
            v->id, intersection_str(v->origin_intersection),
            direction_str(v->origin_direction),
            turn_str(v->destination),
            v->wants_parking ? "yes" : "no");

    /* Check for shutdown */
    if (!sim->simulation_running || g_shutdown_flag) goto cleanup;

    /* ── Phase 1: Parking (if desired and non-emergency) ── */
    if (v->wants_parking && v->priority != PRIORITY_HIGH) {
        handle_parking(sim, v);
        if (!sim->simulation_running || g_shutdown_flag) goto cleanup;
    }

    /* ── Phase 2: Wait for green light ── */
    if (!wait_for_green(sim, v)) goto cleanup;

    /* ── Phase 3: Cross the intersection ── */
    cross_intersection(sim, v);

    /* Clear emergency if applicable */
    if (v->priority == PRIORITY_HIGH) {
        clear_emergency_preemption(sim, v->origin_intersection, v);
    }

    /* ── Phase 4: Travel to other intersection if going East/West ── */
    /* If vehicle at F10 goes East, it arrives at F11 from West (and vice versa) */
    if (!g_shutdown_flag && sim->simulation_running) {
        int travels_to_other = 0;
        IntersectionID other;
        Direction arrival_dir;

        if (v->origin_intersection == F10 &&
            ((v->origin_direction == NORTH && v->destination == GO_RIGHT) ||
             (v->origin_direction == SOUTH && v->destination == GO_LEFT) ||
             (v->origin_direction == WEST && v->destination == GO_STRAIGHT))) {
            travels_to_other = 1;
            other = F11;
            arrival_dir = WEST;
        } else if (v->origin_intersection == F11 &&
                   ((v->origin_direction == NORTH && v->destination == GO_LEFT) ||
                    (v->origin_direction == SOUTH && v->destination == GO_RIGHT) ||
                    (v->origin_direction == EAST && v->destination == GO_STRAIGHT))) {
            travels_to_other = 1;
            other = F10;
            arrival_dir = EAST;
        }

        if (travels_to_other) {
            sim_log(sim, "🚗 %s #%02d traveling from %s to %s",
                    vehicle_type_icon(v->type), v->id,
                    intersection_str(v->origin_intersection),
                    intersection_str(other));

            usleep(800000);  /* Travel time between intersections */

            /* Update vehicle for second intersection */
            v->origin_intersection = other;
            v->origin_direction = arrival_dir;
            v->destination = (Turn)(rand() % NUM_TURNS);
            v->state = VSTATE_APPROACHING;

            /* Emergency preemption at second intersection if needed */
            if (v->priority == PRIORITY_HIGH) {
                /* Notify the other intersection via IPC (pipe) */
                IPCMessage msg;
                msg.type = MSG_EMERGENCY_APPROACHING;
                msg.from = (other == F11) ? F10 : F11;
                msg.direction = arrival_dir;
                msg.vehicle_type = v->type;
                msg.vehicle_id = v->id;

                /* Write to pipe to alert controller */
                if (vargs->pipe_to_ctrl[1] >= 0) {
                    write(vargs->pipe_to_ctrl[1], &msg, sizeof(msg));
                }

                sim_log(sim, "🚨 %s #%02d: %s notified %s of incoming emergency!",
                        vehicle_type_icon(v->type), v->id,
                        intersection_str(msg.from), intersection_str(other));
            }

            /* Wait for green at second intersection */
            if (wait_for_green(sim, v)) {
                cross_intersection(sim, v);
                if (v->priority == PRIORITY_HIGH) {
                    clear_emergency_preemption(sim, v->origin_intersection, v);
                }
            }
        }
    }

cleanup:
    v->state = VSTATE_EXITED;
    v->active = 0;

    sim_log(sim, "%s %s #%02d exited the simulation",
            vehicle_type_icon(v->type), vehicle_type_str(v->type), v->id);

    free(vargs);
    return NULL;
}
