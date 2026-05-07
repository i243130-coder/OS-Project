/*
 * ============================================================================
 *  main.c - Traffic Intersection Simulator Entry Point
 * ============================================================================
 *  This is the main orchestrator for the simulation. It:
 *    1. Allocates shared memory (mmap MAP_SHARED) for simulation state
 *    2. Initializes all mutexes, semaphores, and condition variables
 *    3. Creates bidirectional pipes for inter-controller IPC
 *    4. Forks two child processes (F10 and F11 traffic controllers)
 *    5. Spawns 15 vehicle threads (pthreads)
 *    6. Runs a display thread for real-time visualization
 *    7. Handles graceful shutdown on SIGINT or after all vehicles finish
 *    8. Joins all threads, waits for child processes, cleans up resources
 *
 *  Process Architecture:
 *    ┌──────────────────────────────────────────────┐
 *    │           Parent Process (main)              │
 *    │  ┌────────────────────────────────────────┐  │
 *    │  │  Vehicle Threads (pthread x 15)        │  │
 *    │  │  Display Thread (pthread x 1)          │  │
 *    │  └────────────────────────────────────────┘  │
 *    └───────┬────────────────────┬─────────────────┘
 *            │ pipe               │ pipe
 *    ┌───────▼────────┐  ┌───────▼────────┐
 *    │  F10 Controller│  │  F11 Controller│
 *    │  (child proc)  │◄─►  (child proc)  │
 *    └────────────────┘  └────────────────┘
 *            pipe (bidirectional)
 *
 *  Shared Memory Layout (mmap MAP_SHARED):
 *    SimulationState
 *    ├── intersections[2]  (IntersectionState with process-shared mutex/cond)
 *    ├── parking_lots[2]   (ParkingLot with unnamed semaphores, pshared=1)
 *    ├── log_entries[]     (circular buffer for event log)
 *    └── control flags     (simulation_running, vehicles_spawned, etc.)
 *
 * ============================================================================
 */

#include "common.h"
#include "vehicle.h"
#include "parking.h"
#include "controller.h"
#include "display.h"
#include "gui_display.h"

/* ──────────────────────── Globals ──────────────────────── */

/* Shutdown flag (set by SIGINT handler, read by all threads/processes) */
volatile sig_atomic_t g_shutdown_flag = 0;

/* PIDs of controller child processes (for cleanup) */
static pid_t g_controller_pids[NUM_INTERSECTIONS] = {0, 0};

/* Shared simulation state (allocated via mmap) */
static SimulationState *g_sim = NULL;

/* ──────────────────────── Signal Handler ──────────────────────── */

/*
 * SIGINT handler: sets the shutdown flag for graceful cleanup.
 * All threads and processes periodically check g_shutdown_flag.
 */
static void sigint_handler(int sig) {
    (void)sig;
    g_shutdown_flag = 1;
    if (g_sim) {
        g_sim->simulation_running = 0;
    }
}

/* ──────────────────────── Logging ──────────────────────── */

/*
 * Thread-safe logging to the shared circular log buffer.
 * Called from vehicle threads, controller processes, and main.
 */
void sim_log(SimulationState *sim, const char *fmt, ...) {
    if (!sim) return;

    pthread_mutex_lock(&sim->log_mutex);

    LogEntry *entry = &sim->log_entries[sim->log_head];
    entry->timestamp = time(NULL);

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry->message, LOG_MSG_LEN, fmt, args);
    va_end(args);

    sim->log_head = (sim->log_head + 1) % LOG_BUFFER_SIZE;
    if (sim->log_count < LOG_BUFFER_SIZE)
        sim->log_count++;

    pthread_mutex_unlock(&sim->log_mutex);
}

/* ──────────────────────── Shared Memory Setup ──────────────────────── */

/*
 * Allocate shared memory for SimulationState using mmap.
 * MAP_SHARED | MAP_ANONYMOUS ensures visibility across fork'd processes.
 */
static SimulationState* create_shared_state(void) {
    SimulationState *sim = mmap(NULL, sizeof(SimulationState),
                                PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sim == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    memset(sim, 0, sizeof(SimulationState));

    /* Initialize process-shared mutex attributes */
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    /* Initialize intersection states */
    for (int i = 0; i < NUM_INTERSECTIONS; i++) {
        IntersectionState *inter = &sim->intersections[i];
        inter->id = (IntersectionID)i;
        inter->current_phase = 0;
        inter->emergency_active = 0;
        inter->vehicles_crossing = 0;
        inter->total_crossed = 0;
        inter->emergency_preemptions = 0;

        /* All lights start red */
        for (int d = 0; d < NUM_DIRECTIONS; d++)
            inter->lights[d] = RED;

        pthread_mutex_init(&inter->crossing_mutex, &mattr);
        pthread_cond_init(&inter->crossing_cond, &cattr);
    }

    /* Initialize parking lots */
    for (int i = 0; i < NUM_INTERSECTIONS; i++) {
        parking_init(&sim->parking_lots[i]);
    }

    /* Initialize simulation control */
    sim->simulation_running = 1;
    sim->vehicles_spawned = 0;
    sim->total_vehicles_to_spawn = MAX_VEHICLES;
    pthread_mutex_init(&sim->spawn_mutex, &mattr);

    /* Initialize log */
    sim->log_head = 0;
    sim->log_count = 0;
    pthread_mutex_init(&sim->log_mutex, &mattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    return sim;
}

/*
 * Clean up shared state resources.
 */
static void destroy_shared_state(SimulationState *sim) {
    for (int i = 0; i < NUM_INTERSECTIONS; i++) {
        pthread_mutex_destroy(&sim->intersections[i].crossing_mutex);
        pthread_cond_destroy(&sim->intersections[i].crossing_cond);
        parking_destroy(&sim->parking_lots[i]);
    }
    pthread_mutex_destroy(&sim->spawn_mutex);
    pthread_mutex_destroy(&sim->log_mutex);
    munmap(sim, sizeof(SimulationState));
}

/* ──────────────────────── Main ──────────────────────── */

int main(int argc, char *argv[]) {
    int num_vehicles = MAX_VEHICLES;

    /* Parse optional command line argument for vehicle count */
    if (argc > 1) {
        num_vehicles = atoi(argv[1]);
        if (num_vehicles <= 0 || num_vehicles > 50) {
            fprintf(stderr, "Usage: %s [num_vehicles (1-50, default=%d)]\n",
                    argv[0], MAX_VEHICLES);
            return EXIT_FAILURE;
        }
    }

    printf(ANSI_HIDE_CURSOR);
    printf(ANSI_CLEAR ANSI_HOME);
    printf(ANSI_BOLD ANSI_CYAN
           "\n  Initializing Traffic Intersection Simulator...\n\n"
           ANSI_RESET);

    /* Seed random number generator */
    srand(time(NULL) ^ getpid());

    /* ── Step 1: Create shared memory ── */
    g_sim = create_shared_state();
    g_sim->total_vehicles_to_spawn = num_vehicles;

    sim_log(g_sim, "🏁 Simulation initialized with %d vehicles", num_vehicles);

    /* ── Step 2: Set up signal handler ── */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /* ── Step 3: Create pipes for IPC ── */
    /*
     * Pipe layout:
     *   pipe_f10_to_f11[0/1]: F10 controller writes, F11 controller reads
     *   pipe_f11_to_f10[0/1]: F11 controller writes, F10 controller reads
     *   pipe_emergency[0/1]:  vehicles write, controllers read
     */
    int pipe_f10_to_f11[2], pipe_f11_to_f10[2];
    int pipe_emergency_f10[2], pipe_emergency_f11[2];

    if (pipe(pipe_f10_to_f11) < 0 || pipe(pipe_f11_to_f10) < 0 ||
        pipe(pipe_emergency_f10) < 0 || pipe(pipe_emergency_f11) < 0) {
        perror("pipe");
        destroy_shared_state(g_sim);
        return EXIT_FAILURE;
    }

    sim_log(g_sim, "📡 IPC pipes created for inter-controller communication");

    /* ── Step 4: Fork F10 controller process ── */
    pid_t pid_f10 = fork();
    if (pid_f10 < 0) {
        perror("fork(F10)");
        destroy_shared_state(g_sim);
        return EXIT_FAILURE;
    }

    if (pid_f10 == 0) {
        /* ── Child Process: F10 Controller ── */
        /* Close unused pipe ends */
        close(pipe_f10_to_f11[0]);  /* F10 doesn't read from f10_to_f11 */
        close(pipe_f11_to_f10[1]);  /* F10 doesn't write to f11_to_f10 */
        close(pipe_emergency_f11[0]);
        close(pipe_emergency_f11[1]);
        close(pipe_emergency_f10[1]);

        ControllerArgs args;
        args.id = F10;
        args.sim = g_sim;
        args.pipe_read_fd = pipe_f11_to_f10[0];   /* Read from F11 */
        args.pipe_write_fd = pipe_f10_to_f11[1];   /* Write to F11 */
        args.emergency_pipe_read_fd = pipe_emergency_f10[0];

        controller_process(&args);

        close(pipe_f11_to_f10[0]);
        close(pipe_f10_to_f11[1]);
        close(pipe_emergency_f10[0]);
        _exit(0);
    }

    g_controller_pids[F10] = pid_f10;
    sim_log(g_sim, "🚦 F10 Controller forked (PID: %d)", pid_f10);

    /* ── Step 5: Fork F11 controller process ── */
    pid_t pid_f11 = fork();
    if (pid_f11 < 0) {
        perror("fork(F11)");
        kill(pid_f10, SIGTERM);
        destroy_shared_state(g_sim);
        return EXIT_FAILURE;
    }

    if (pid_f11 == 0) {
        /* ── Child Process: F11 Controller ── */
        close(pipe_f11_to_f10[0]);  /* F11 doesn't read from f11_to_f10 */
        close(pipe_f10_to_f11[1]);  /* F11 doesn't write to f10_to_f11 */
        close(pipe_emergency_f10[0]);
        close(pipe_emergency_f10[1]);
        close(pipe_emergency_f11[1]);

        ControllerArgs args;
        args.id = F11;
        args.sim = g_sim;
        args.pipe_read_fd = pipe_f10_to_f11[0];   /* Read from F10 */
        args.pipe_write_fd = pipe_f11_to_f10[1];   /* Write to F10 */
        args.emergency_pipe_read_fd = pipe_emergency_f11[0];

        controller_process(&args);

        close(pipe_f10_to_f11[0]);
        close(pipe_f11_to_f10[1]);
        close(pipe_emergency_f11[0]);
        _exit(0);
    }

    g_controller_pids[F11] = pid_f11;
    sim_log(g_sim, "🚦 F11 Controller forked (PID: %d)", pid_f11);

    /* Parent closes unused pipe ends */
    close(pipe_f10_to_f11[0]);
    close(pipe_f10_to_f11[1]);
    close(pipe_f11_to_f10[0]);
    close(pipe_f11_to_f10[1]);
    close(pipe_emergency_f10[0]);
    close(pipe_emergency_f11[0]);

    /* Small delay to let controllers initialize */
    usleep(500000);

    /* ── Step 6: Create vehicle array and threads ── */
    Vehicle vehicles[num_vehicles];
    memset(vehicles, 0, sizeof(Vehicle) * num_vehicles);

    /* Start console display thread */
    pthread_t display_tid;
    DisplayArgs dargs;
    dargs.sim = g_sim;
    dargs.vehicles = vehicles;
    dargs.num_vehicles = num_vehicles;
    pthread_create(&display_tid, NULL, display_thread, &dargs);

    /* Start SFML GUI display thread */
    pthread_t gui_tid;
    GUIDisplayArgs gdargs;
    gdargs.sim = g_sim;
    gdargs.vehicles = vehicles;
    gdargs.num_vehicles = num_vehicles;
    pthread_create(&gui_tid, NULL, gui_display_thread, &gdargs);

    /* ── Step 7: Spawn vehicle threads at random intervals ── */
    for (int i = 0; i < num_vehicles; i++) {
        if (g_shutdown_flag) break;

        /* Initialize vehicle with random attributes */
        vehicle_init(&vehicles[i], i + 1);

        /* Create vehicle thread arguments */
        VehicleArgs *vargs = malloc(sizeof(VehicleArgs));
        vargs->vehicle = &vehicles[i];
        vargs->sim = g_sim;

        /* Give the vehicle access to emergency pipe for its intersection */
        if (vehicles[i].origin_intersection == F10) {
            vargs->pipe_to_ctrl[0] = -1;
            vargs->pipe_to_ctrl[1] = pipe_emergency_f10[1];
        } else {
            vargs->pipe_to_ctrl[0] = -1;
            vargs->pipe_to_ctrl[1] = pipe_emergency_f11[1];
        }

        pthread_create(&vehicles[i].thread, NULL, vehicle_thread, vargs);

        pthread_mutex_lock(&g_sim->spawn_mutex);
        g_sim->vehicles_spawned++;
        pthread_mutex_unlock(&g_sim->spawn_mutex);

        /* Random interval between spawns */
        int spawn_delay = SPAWN_INTERVAL_MIN +
                          (rand() % (SPAWN_INTERVAL_MAX - SPAWN_INTERVAL_MIN + 1));
        sleep(spawn_delay);
    }

    /* ── Step 8: Wait for all vehicle threads to finish ── */
    sim_log(g_sim, "⏳ All vehicles spawned, waiting for completion...");

    for (int i = 0; i < num_vehicles; i++) {
        if (vehicles[i].thread) {
            pthread_join(vehicles[i].thread, NULL);
        }
    }

    sim_log(g_sim, "✅ All vehicle threads joined");

    /* ── Step 9: Graceful shutdown ── */
    g_sim->simulation_running = 0;

    /* Stop display threads */
    pthread_join(display_tid, NULL);
    pthread_join(gui_tid, NULL);

    /* Close remaining pipe ends */
    close(pipe_emergency_f10[1]);
    close(pipe_emergency_f11[1]);

    /* Terminate controller processes */
    for (int i = 0; i < NUM_INTERSECTIONS; i++) {
        if (g_controller_pids[i] > 0) {
            kill(g_controller_pids[i], SIGTERM);
            int status;
            waitpid(g_controller_pids[i], &status, 0);
            sim_log(g_sim, "🚦 %s Controller stopped (PID: %d)",
                    intersection_str(i), g_controller_pids[i]);
        }
    }

    /* ── Step 10: Display final summary ── */
    printf(ANSI_SHOW_CURSOR);
    display_final_summary(g_sim, vehicles, num_vehicles);

    /* ── Step 11: Clean up shared memory and IPC resources ── */
    destroy_shared_state(g_sim);
    g_sim = NULL;

    printf(ANSI_GREEN ANSI_BOLD
           "  ✓ Simulation terminated gracefully. All resources freed.\n\n"
           ANSI_RESET);

    return EXIT_SUCCESS;
}
