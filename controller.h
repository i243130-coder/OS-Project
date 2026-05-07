/*
 * ============================================================================
 *  controller.h - Traffic Controller Process Interface
 * ============================================================================
 *  Each intersection has its own traffic controller, implemented as a
 *  separate process (spawned via fork()). Controllers:
 *    - Cycle traffic lights through phases
 *    - Handle emergency preemption
 *    - Communicate with each other via pipes for coordination
 * ============================================================================
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "common.h"

/*
 * Arguments for the controller process.
 */
typedef struct {
    IntersectionID   id;             /* Which intersection this controls  */
    SimulationState *sim;            /* Shared simulation state           */
    int              pipe_read_fd;   /* Pipe: read from other controller  */
    int              pipe_write_fd;  /* Pipe: write to other controller   */
    int              emergency_pipe_read_fd;  /* Pipe: read from vehicles */
} ControllerArgs;

/*
 * Controller process main function.
 * Runs in a child process created by fork().
 */
void controller_process(ControllerArgs *args);

#endif /* CONTROLLER_H */
