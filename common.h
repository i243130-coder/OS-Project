/*
 * ============================================================================
 *  Traffic Intersection Simulator - F10 & F11
 *  common.h - Shared definitions, types, constants, and macros
 * ============================================================================
 *  This header defines all shared data structures used across the simulation
 *  including vehicle metadata, intersection state, parking lots, and IPC.
 * ============================================================================
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>

/* ──────────────────────── Configuration Constants ──────────────────────── */

#define MAX_VEHICLES          15    /* Default number of vehicle threads     */
#define PARKING_SPOTS         10    /* Fixed parking capacity per lot        */
#define PARKING_QUEUE_SIZE     5    /* Bounded waiting queue size            */
#define NUM_INTERSECTIONS      2    /* F10 and F11                           */
#define PHASE_DURATION_SEC     3    /* Duration of each traffic phase (sec)  */
#define YELLOW_DURATION_SEC    1    /* Yellow light duration (sec)           */
#define PARK_DURATION_MIN      2    /* Min parking duration (sec)            */
#define PARK_DURATION_MAX      5    /* Max parking duration (sec)            */
#define CROSSING_TIME_SEC      2    /* Time to cross an intersection (sec)   */
#define SPAWN_INTERVAL_MIN     1    /* Min spawn interval (sec)             */
#define SPAWN_INTERVAL_MAX     3    /* Max spawn interval (sec)             */
#define LOG_BUFFER_SIZE       20    /* Number of log entries to display      */
#define LOG_MSG_LEN          256    /* Max length of a log message           */

/* ──────────────────────── Enumerations ──────────────────────── */

/* Vehicle categories */
typedef enum {
    AMBULANCE  = 0,
    FIRETRUCK  = 1,
    BUS        = 2,
    CAR        = 3,
    BIKE       = 4,
    TRACTOR    = 5,
    NUM_VEHICLE_TYPES = 6
} VehicleType;

/* Compass directions for intersection approaches */
typedef enum {
    NORTH = 0,
    SOUTH = 1,
    EAST  = 2,
    WEST  = 3,
    NUM_DIRECTIONS = 4
} Direction;

/* Turn intentions */
typedef enum {
    GO_STRAIGHT = 0,
    GO_LEFT     = 1,
    GO_RIGHT    = 2,
    NUM_TURNS   = 3
} Turn;

/* Intersection identifiers */
typedef enum {
    F10 = 0,
    F11 = 1
} IntersectionID;

/* Priority levels */
typedef enum {
    PRIORITY_HIGH   = 0,   /* Ambulance, Firetruck                 */
    PRIORITY_MEDIUM = 1,   /* Bus                                   */
    PRIORITY_LOW    = 2    /* Car, Bike, Tractor                    */
} Priority;

/* Traffic light states */
typedef enum {
    RED    = 0,
    GREEN  = 1,
    YELLOW = 2
} LightState;

/* Vehicle lifecycle states */
typedef enum {
    VSTATE_SPAWNED     = 0,
    VSTATE_APPROACHING = 1,
    VSTATE_QUEUE_PARK  = 2,   /* Waiting in parking queue              */
    VSTATE_PARKED      = 3,
    VSTATE_WAITING     = 4,   /* Waiting at intersection for green     */
    VSTATE_CROSSING    = 5,
    VSTATE_CROSSED     = 6,
    VSTATE_EXITED      = 7
} VehicleState;

/* IPC message types for pipe communication */
typedef enum {
    MSG_EMERGENCY_APPROACHING = 1,
    MSG_EMERGENCY_CLEARED     = 2,
    MSG_STATUS_UPDATE         = 3,
    MSG_PHASE_CHANGE          = 4,
    MSG_SHUTDOWN              = 5
} MessageType;

/* ──────────────────────── Data Structures ──────────────────────── */

/*
 * Vehicle - represents a single vehicle thread's metadata.
 * Each vehicle is a pthread; this struct holds all per-vehicle state.
 */
typedef struct {
    int            id;                    /* Unique vehicle ID              */
    VehicleType    type;                  /* Category of vehicle            */
    IntersectionID origin_intersection;   /* Starting intersection          */
    Direction      origin_direction;      /* Approach direction             */
    Turn           destination;           /* Intended turn                  */
    Priority       priority;              /* Priority level                 */
    time_t         arrival_time;          /* Timestamp of arrival           */
    int            wants_parking;         /* 1 if vehicle intends to park   */
    VehicleState   state;                 /* Current lifecycle state        */
    int            active;                /* 1 if thread is active          */
    pthread_t      thread;               /* Thread handle                  */
} Vehicle;

/*
 * IntersectionState - shared state for one intersection.
 * Accessed by both controller process and vehicle threads via shared memory.
 */
typedef struct {
    IntersectionID  id;

    /* Traffic light state per direction */
    LightState      lights[NUM_DIRECTIONS];
    int             current_phase;            /* 0=NS, 1=EW              */

    /* Emergency preemption state */
    int             emergency_active;
    Direction       emergency_direction;
    VehicleType     emergency_vehicle_type;
    int             emergency_vehicle_id;

    /* Crossing synchronization */
    pthread_mutex_t crossing_mutex;
    pthread_cond_t  crossing_cond;

    /* Number of vehicles currently crossing */
    int             vehicles_crossing;

    /* Statistics */
    int             total_crossed;
    int             emergency_preemptions;

} IntersectionState;

/*
 * ParkingLot - parking lot state with dual semaphores.
 * spots_sem: counts available parking spots (init=10)
 * queue_sem: counts available queue slots (init=5)
 */
typedef struct {
    sem_t           spots_sem;            /* Available parking spots       */
    sem_t           queue_sem;            /* Available queue slots         */
    int             occupied_spots;       /* Currently occupied spots      */
    int             vehicles_in_queue;    /* Currently in waiting queue    */
    pthread_mutex_t park_mutex;           /* Protects counters             */
} ParkingLot;

/*
 * IPCMessage - message format for pipe communication between controllers.
 */
typedef struct {
    MessageType     type;
    IntersectionID  from;
    Direction       direction;            /* Emergency approach direction  */
    VehicleType     vehicle_type;
    int             vehicle_id;
    int             phase;                /* Current phase info            */
} IPCMessage;

/*
 * LogEntry - a single log entry for the event display.
 */
typedef struct {
    char            message[LOG_MSG_LEN];
    time_t          timestamp;
} LogEntry;

/*
 * SimulationState - top-level shared state for the entire simulation.
 * Allocated in shared memory (mmap MAP_SHARED) so both parent process
 * (vehicle threads) and child processes (controllers) can access it.
 */
typedef struct {
    IntersectionState intersections[NUM_INTERSECTIONS];
    ParkingLot        parking_lots[NUM_INTERSECTIONS];

    /* Simulation control */
    int               simulation_running;
    int               vehicles_spawned;
    int               total_vehicles_to_spawn;
    pthread_mutex_t   spawn_mutex;

    /* Event log (circular buffer) */
    LogEntry          log_entries[LOG_BUFFER_SIZE];
    int               log_head;
    int               log_count;
    pthread_mutex_t   log_mutex;

} SimulationState;

/* ──────────────────────── Global Variables ──────────────────────── */

/* Global shutdown flag set by SIGINT handler */
extern volatile sig_atomic_t g_shutdown_flag;

/* ──────────────────────── Helper Macros ──────────────────────── */

/* String representations */
static inline const char* vehicle_type_str(VehicleType t) {
    static const char* names[] = {
        "Ambulance", "Firetruck", "Bus", "Car", "Bike", "Tractor"
    };
    return (t < NUM_VEHICLE_TYPES) ? names[t] : "Unknown";
}

static inline const char* vehicle_type_icon(VehicleType t) {
    static const char* icons[] = {
        "🚑", "🚒", "🚌", "🚗", "🏍 ", "🚜"
    };
    return (t < NUM_VEHICLE_TYPES) ? icons[t] : "❓";
}

static inline const char* direction_str(Direction d) {
    static const char* names[] = { "North", "South", "East", "West" };
    return (d < NUM_DIRECTIONS) ? names[d] : "Unknown";
}

static inline const char* direction_short(Direction d) {
    static const char* names[] = { "N", "S", "E", "W" };
    return (d < NUM_DIRECTIONS) ? names[d] : "?";
}

static inline const char* turn_str(Turn t) {
    static const char* names[] = { "Straight", "Left", "Right" };
    return (t < NUM_TURNS) ? names[t] : "Unknown";
}

static inline const char* intersection_str(IntersectionID id) {
    return (id == F10) ? "F10" : "F11";
}

static inline const char* light_str(LightState l) {
    switch (l) {
        case RED:    return "🔴";
        case GREEN:  return "🟢";
        case YELLOW: return "🟡";
        default:     return "⚫";
    }
}

static inline const char* state_str(VehicleState s) {
    static const char* names[] = {
        "SPAWNED", "APPROACHING", "QUEUE_PARK", "PARKED",
        "WAITING", "CROSSING", "CROSSED", "EXITED"
    };
    return (s <= VSTATE_EXITED) ? names[s] : "UNKNOWN";
}

static inline Priority get_vehicle_priority(VehicleType t) {
    switch (t) {
        case AMBULANCE: case FIRETRUCK: return PRIORITY_HIGH;
        case BUS:                        return PRIORITY_MEDIUM;
        default:                         return PRIORITY_LOW;
    }
}

/* ANSI Color codes for terminal output */
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_RED         "\033[31m"
#define ANSI_GREEN       "\033[32m"
#define ANSI_YELLOW      "\033[33m"
#define ANSI_BLUE        "\033[34m"
#define ANSI_MAGENTA     "\033[35m"
#define ANSI_CYAN        "\033[36m"
#define ANSI_WHITE       "\033[37m"
#define ANSI_BG_RED      "\033[41m"
#define ANSI_BG_GREEN    "\033[42m"
#define ANSI_BG_YELLOW   "\033[43m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_MAGENTA  "\033[45m"
#define ANSI_BG_CYAN     "\033[46m"
#define ANSI_CLEAR       "\033[2J"
#define ANSI_HOME        "\033[H"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"

/* Priority color */
static inline const char* priority_color(Priority p) {
    switch (p) {
        case PRIORITY_HIGH:   return ANSI_RED;
        case PRIORITY_MEDIUM: return ANSI_YELLOW;
        case PRIORITY_LOW:    return ANSI_GREEN;
        default:              return ANSI_WHITE;
    }
}

#endif /* COMMON_H */
