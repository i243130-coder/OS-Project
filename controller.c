/*
 * ============================================================================
 *  controller.c - Traffic Controller Process Implementation
 * ============================================================================
 *  Runs as a child process (forked from parent). Manages traffic light
 *  cycling and handles emergency preemption via pipe-based IPC.
 * ============================================================================
 */

#include "controller.h"

/* Forward declaration for logging */
extern void sim_log(SimulationState *sim, const char *fmt, ...);

/*
 * Set traffic lights for a given phase.
 * Phase 0: North-South green, East-West red
 * Phase 1: East-West green, North-South red
 */
static void set_phase(IntersectionState *inter, int phase) {
    pthread_mutex_lock(&inter->crossing_mutex);

    if (phase == 0) {
        /* NS Green */
        inter->lights[NORTH] = GREEN;
        inter->lights[SOUTH] = GREEN;
        inter->lights[EAST]  = RED;
        inter->lights[WEST]  = RED;
    } else {
        /* EW Green */
        inter->lights[NORTH] = RED;
        inter->lights[SOUTH] = RED;
        inter->lights[EAST]  = GREEN;
        inter->lights[WEST]  = GREEN;
    }

    inter->current_phase = phase;
    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);
}

/*
 * Set all lights to red (used during transitions and emergencies).
 */
static void set_all_red(IntersectionState *inter) {
    pthread_mutex_lock(&inter->crossing_mutex);
    for (int i = 0; i < NUM_DIRECTIONS; i++)
        inter->lights[i] = RED;
    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);
}

/*
 * Set yellow for the currently green directions (transition).
 */
static void set_yellow_transition(IntersectionState *inter) {
    pthread_mutex_lock(&inter->crossing_mutex);
    for (int i = 0; i < NUM_DIRECTIONS; i++) {
        if (inter->lights[i] == GREEN)
            inter->lights[i] = YELLOW;
    }
    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);
}

/*
 * Handle emergency preemption.
 * Gives green to the emergency direction and red to all others.
 */
static void handle_emergency(IntersectionState *inter,
                             Direction edir, SimulationState *sim) {
    /* Set all red first */
    set_all_red(inter);
    usleep(300000);

    /* Wait for crossing vehicles to clear */
    pthread_mutex_lock(&inter->crossing_mutex);
    while (inter->vehicles_crossing > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_cond_timedwait(&inter->crossing_cond,
                               &inter->crossing_mutex, &ts);
    }

    /* Give green to emergency direction */
    inter->lights[edir] = GREEN;
    /* Also green for the opposite direction (non-conflicting) */
    Direction opp = (edir == NORTH) ? SOUTH :
                    (edir == SOUTH) ? NORTH :
                    (edir == EAST)  ? WEST  : EAST;
    inter->lights[opp] = GREEN;

    pthread_cond_broadcast(&inter->crossing_cond);
    pthread_mutex_unlock(&inter->crossing_mutex);

    sim_log(sim, "🚨 %s controller: EMERGENCY green for %s direction",
            intersection_str(inter->id), direction_str(edir));
}

/*
 * Check for incoming IPC messages on pipe (non-blocking).
 * Returns 1 if an emergency message was received, 0 otherwise.
 */
static int check_pipe_messages(int pipe_fd, int emergency_pipe_fd,
                               IPCMessage *msg_out) {
    fd_set readfds;
    struct timeval tv = {0, 50000}; /* 50ms timeout */

    FD_ZERO(&readfds);
    if (pipe_fd >= 0) FD_SET(pipe_fd, &readfds);
    if (emergency_pipe_fd >= 0) FD_SET(emergency_pipe_fd, &readfds);

    int maxfd = (pipe_fd > emergency_pipe_fd) ? pipe_fd : emergency_pipe_fd;
    if (maxfd < 0) return 0;

    int ret = select(maxfd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) return 0;

    /* Check controller-to-controller pipe */
    if (pipe_fd >= 0 && FD_ISSET(pipe_fd, &readfds)) {
        if (read(pipe_fd, msg_out, sizeof(IPCMessage)) == sizeof(IPCMessage)) {
            return (msg_out->type == MSG_EMERGENCY_APPROACHING) ? 1 : 0;
        }
    }

    /* Check vehicle-to-controller emergency pipe */
    if (emergency_pipe_fd >= 0 && FD_ISSET(emergency_pipe_fd, &readfds)) {
        if (read(emergency_pipe_fd, msg_out, sizeof(IPCMessage)) == sizeof(IPCMessage)) {
            return (msg_out->type == MSG_EMERGENCY_APPROACHING) ? 1 : 0;
        }
    }

    return 0;
}

/*
 * Main controller process function.
 * Runs the traffic light cycling loop with emergency preemption support.
 */
void controller_process(ControllerArgs *args) {
    IntersectionState *inter = &args->sim->intersections[args->id];
    SimulationState *sim = args->sim;
    int phase = 0;

    sim_log(sim, "🚦 %s Traffic Controller started (PID: %d)",
            intersection_str(args->id), getpid());

    /* Initial phase: NS green */
    set_phase(inter, 0);

    while (sim->simulation_running && !g_shutdown_flag) {
        /* Check for emergency messages */
        IPCMessage msg;
        if (check_pipe_messages(args->pipe_read_fd,
                                args->emergency_pipe_read_fd, &msg)) {
            sim_log(sim, "🚨 %s controller received emergency alert from %s!",
                    intersection_str(args->id), intersection_str(msg.from));

            /* Forward to the other controller if this is a transit emergency */
            if (msg.from != args->id && args->pipe_write_fd >= 0) {
                IPCMessage fwd = msg;
                fwd.from = args->id;
                write(args->pipe_write_fd, &fwd, sizeof(fwd));
            }

            /* Handle emergency preemption */
            handle_emergency(inter, msg.direction, sim);

            /* Wait for emergency to pass */
            int wait_count = 0;
            while (inter->emergency_active && wait_count < 10 &&
                   sim->simulation_running && !g_shutdown_flag) {
                usleep(500000);
                wait_count++;
            }

            sim_log(sim, "🚦 %s controller: emergency cleared, resuming normal ops",
                    intersection_str(args->id));
        }

        /* Check for direct emergency flag (set by vehicle threads) */
        pthread_mutex_lock(&inter->crossing_mutex);
        int emerg = inter->emergency_active;
        Direction edir = inter->emergency_direction;
        pthread_mutex_unlock(&inter->crossing_mutex);

        if (emerg) {
            handle_emergency(inter, edir, sim);

            int wait_count = 0;
            while (inter->emergency_active && wait_count < 10 &&
                   sim->simulation_running && !g_shutdown_flag) {
                usleep(500000);
                wait_count++;
            }
            continue;
        }

        /* Normal phase cycling */
        /* Yellow transition */
        set_yellow_transition(inter);
        sleep(YELLOW_DURATION_SEC);

        /* Switch phase */
        phase = (phase + 1) % 2;
        set_phase(inter, phase);

        sim_log(sim, "🚦 %s: Phase → %s green",
                intersection_str(args->id),
                (phase == 0) ? "N-S" : "E-W");

        /* Hold phase for configured duration */
        for (int i = 0; i < PHASE_DURATION_SEC * 2; i++) {
            if (!sim->simulation_running || g_shutdown_flag) break;

            /* Check for emergency mid-phase */
            pthread_mutex_lock(&inter->crossing_mutex);
            emerg = inter->emergency_active;
            pthread_mutex_unlock(&inter->crossing_mutex);
            if (emerg) break;

            /* Check pipe for emergency mid-phase */
            if (check_pipe_messages(args->pipe_read_fd,
                                    args->emergency_pipe_read_fd, &msg)) {
                if (msg.type == MSG_EMERGENCY_APPROACHING) {
                    handle_emergency(inter, msg.direction, sim);
                    break;
                }
            }

            usleep(500000); /* 500ms */
        }
    }

    /* Shutdown: set all lights to red */
    set_all_red(inter);

    sim_log(sim, "🚦 %s Traffic Controller stopped (PID: %d)",
            intersection_str(args->id), getpid());
}
