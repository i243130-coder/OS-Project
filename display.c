/*
 * ============================================================================
 *  display.c - Terminal Visualization Module Implementation
 * ============================================================================
 *  Renders a real-time dashboard showing:
 *    - Both intersections with traffic light states
 *    - Parking lot occupancy bars
 *    - Active vehicle status table
 *    - Scrolling event log
 *  Uses ANSI escape codes for colors, box drawing, and cursor control.
 * ============================================================================
 */

#include "display.h"
#include "parking.h"

/* ──────────────── Helper: print a horizontal bar ──────────────── */
static void print_bar(int filled, int total, const char *fill_color) {
    printf("%s", fill_color);
    for (int i = 0; i < total; i++) {
        if (i < filled) printf("█");
        else printf(ANSI_DIM "░" ANSI_RESET "%s", fill_color);
    }
    printf(ANSI_RESET);
}

/* ──────────────── Helper: traffic light color ──────────────── */
static const char* light_ansi(LightState l) {
    switch (l) {
        case RED:    return ANSI_RED;
        case GREEN:  return ANSI_GREEN;
        case YELLOW: return ANSI_YELLOW;
        default:     return ANSI_WHITE;
    }
}

/* ──────────────── Helper: state color ──────────────── */
static const char* state_color(VehicleState s) {
    switch (s) {
        case VSTATE_CROSSING:    return ANSI_GREEN;
        case VSTATE_PARKED:      return ANSI_CYAN;
        case VSTATE_QUEUE_PARK:  return ANSI_MAGENTA;
        case VSTATE_WAITING:     return ANSI_YELLOW;
        case VSTATE_APPROACHING: return ANSI_BLUE;
        case VSTATE_EXITED:      return ANSI_DIM;
        default:                 return ANSI_WHITE;
    }
}

/* ──────────────── Render one intersection block ──────────────── */
static void render_intersection(IntersectionState *inter, ParkingLot *lot,
                                const char *label) {
    int occ, inq;
    parking_get_status(lot, &occ, &inq);

    /* Intersection header */
    printf("  " ANSI_BOLD ANSI_CYAN "═══ %s INTERSECTION ═══" ANSI_RESET "\n", label);

    /* Traffic lights */
    printf("  ");
    for (int d = 0; d < NUM_DIRECTIONS; d++) {
        printf(" %s:%s%s%s ",
               direction_short(d),
               light_ansi(inter->lights[d]),
               light_str(inter->lights[d]),
               ANSI_RESET);
    }
    printf("\n");

    /* Phase and crossing info */
    printf("  Phase: " ANSI_BOLD "%s" ANSI_RESET
           "  Crossing: " ANSI_BOLD "%d" ANSI_RESET
           "  Total: " ANSI_BOLD "%d" ANSI_RESET "\n",
           (inter->current_phase == 0) ? "N-S Green" : "E-W Green",
           inter->vehicles_crossing,
           inter->total_crossed);

    /* Emergency status */
    if (inter->emergency_active) {
        printf("  " ANSI_BG_RED ANSI_WHITE ANSI_BOLD
               " 🚨 EMERGENCY ACTIVE - %s "
               ANSI_RESET "\n",
               direction_str(inter->emergency_direction));
    } else {
        printf("  " ANSI_GREEN "● Normal Operation" ANSI_RESET "\n");
    }

    /* Parking lot */
    printf("  " ANSI_BOLD "Parking:" ANSI_RESET " [");
    print_bar(occ, PARKING_SPOTS, ANSI_BLUE);
    printf("] %d/%d", occ, PARKING_SPOTS);

    printf("  Queue: [");
    print_bar(inq, PARKING_QUEUE_SIZE, ANSI_MAGENTA);
    printf("] %d/%d\n", inq, PARKING_QUEUE_SIZE);
}

/* ──────────────── Render ASCII intersection map ──────────────── */
static void render_intersection_map(IntersectionState *f10,
                                    IntersectionState *f11) {
    const char *n10 = light_ansi(f10->lights[NORTH]);
    const char *s10 = light_ansi(f10->lights[SOUTH]);
    const char *e10 = light_ansi(f10->lights[EAST]);
    const char *w10 = light_ansi(f10->lights[WEST]);

    const char *n11 = light_ansi(f11->lights[NORTH]);
    const char *s11 = light_ansi(f11->lights[SOUTH]);
    const char *e11 = light_ansi(f11->lights[EAST]);
    const char *w11 = light_ansi(f11->lights[WEST]);

    printf("        %s│%s            %s│%s", n10, ANSI_RESET, n10, ANSI_RESET);
    printf("                     %s│%s            %s│%s\n", n11, ANSI_RESET, n11, ANSI_RESET);

    printf("        %s│%s     N      %s│%s", n10, ANSI_RESET, n10, ANSI_RESET);
    printf("                     %s│%s     N      %s│%s\n", n11, ANSI_RESET, n11, ANSI_RESET);

    printf("  %s──────%s┼%s────────────%s┼%s──────%s", w10, ANSI_RESET, w10, ANSI_RESET, e10, ANSI_RESET);
    printf("  %s═══════%s", ANSI_DIM, ANSI_RESET);
    printf("  %s──────%s┼%s────────────%s┼%s──────%s\n", w11, ANSI_RESET, w11, ANSI_RESET, e11, ANSI_RESET);

    printf("  W     %s│%s   " ANSI_BOLD "F10" ANSI_RESET "    %s│%s     E", w10, ANSI_RESET, e10, ANSI_RESET);
    printf("     ←→     ");
    printf("  W     %s│%s   " ANSI_BOLD "F11" ANSI_RESET "    %s│%s     E\n", w11, ANSI_RESET, e11, ANSI_RESET);

    printf("  %s──────%s┼%s────────────%s┼%s──────%s", w10, ANSI_RESET, w10, ANSI_RESET, e10, ANSI_RESET);
    printf("  %s═══════%s", ANSI_DIM, ANSI_RESET);
    printf("  %s──────%s┼%s────────────%s┼%s──────%s\n", w11, ANSI_RESET, w11, ANSI_RESET, e11, ANSI_RESET);

    printf("        %s│%s     S      %s│%s", s10, ANSI_RESET, s10, ANSI_RESET);
    printf("                     %s│%s     S      %s│%s\n", s11, ANSI_RESET, s11, ANSI_RESET);

    printf("        %s│%s            %s│%s", s10, ANSI_RESET, s10, ANSI_RESET);
    printf("                     %s│%s            %s│%s\n", s11, ANSI_RESET, s11, ANSI_RESET);
}

/* ──────────────── Render vehicle table ──────────────── */
static void render_vehicles(Vehicle *vehicles, int n) {
    printf("  " ANSI_BOLD ANSI_CYAN
           "ID  Type        Priority  Location       Direction  Turn      State"
           ANSI_RESET "\n");
    printf("  " ANSI_DIM
           "──  ──────────  ────────  ─────────────  ─────────  ────────  ──────────"
           ANSI_RESET "\n");

    for (int i = 0; i < n; i++) {
        Vehicle *v = &vehicles[i];
        if (v->state == VSTATE_SPAWNED && !v->active) continue;

        printf("  %s#%02d %-10s  %-8s  %-13s  %-9s  %-8s  %s%-10s%s\n",
               vehicle_type_icon(v->type),
               v->id,
               vehicle_type_str(v->type),
               (v->priority == PRIORITY_HIGH) ? "HIGH" :
               (v->priority == PRIORITY_MEDIUM) ? "MEDIUM" : "LOW",
               intersection_str(v->origin_intersection),
               direction_str(v->origin_direction),
               turn_str(v->destination),
               state_color(v->state),
               state_str(v->state),
               ANSI_RESET);
    }
}

/* ──────────────── Render event log ──────────────── */
static void render_log(SimulationState *sim) {
    pthread_mutex_lock(&sim->log_mutex);

    int count = sim->log_count;
    if (count > 10) count = 10; /* Show last 10 entries */

    for (int i = 0; i < count; i++) {
        int idx = (sim->log_head - count + i + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
        LogEntry *e = &sim->log_entries[idx];

        struct tm *tm = localtime(&e->timestamp);
        printf("  " ANSI_DIM "[%02d:%02d:%02d]" ANSI_RESET " %s\n",
               tm->tm_hour, tm->tm_min, tm->tm_sec,
               e->message);
    }

    pthread_mutex_unlock(&sim->log_mutex);
}

/* ──────────────── Main display thread ──────────────── */
void* display_thread(void *arg) {
    DisplayArgs *dargs = (DisplayArgs *)arg;
    SimulationState *sim = dargs->sim;
    Vehicle *vehicles = dargs->vehicles;
    int n = dargs->num_vehicles;

    while (sim->simulation_running && !g_shutdown_flag) {
        /* Clear screen and home cursor */
        printf(ANSI_HOME ANSI_CLEAR);

        /* Title bar */
        printf(ANSI_BOLD ANSI_BG_BLUE ANSI_WHITE);
        printf("  ╔══════════════════════════════════════════════════════════════════════════════╗\n");
        printf("  ║          🚦  TRAFFIC INTERSECTION SIMULATOR  —  F10 & F11  🚦               ║\n");
        printf("  ╚══════════════════════════════════════════════════════════════════════════════╝");
        printf(ANSI_RESET "\n\n");

        /* Intersection map */
        render_intersection_map(&sim->intersections[F10],
                                &sim->intersections[F11]);
        printf("\n");

        /* Intersection details side by side */
        printf("  ┌─────────────────────────────────────┬─────────────────────────────────────┐\n");
        printf("  │                                     │                                     │\n");

        /* F10 details */
        render_intersection(&sim->intersections[F10],
                            &sim->parking_lots[F10], "F10");
        printf("\n");

        /* F11 details */
        render_intersection(&sim->intersections[F11],
                            &sim->parking_lots[F11], "F11");
        printf("\n");

        /* Vehicles table */
        printf("  " ANSI_BOLD ANSI_BG_CYAN ANSI_WHITE
               " 🚗 ACTIVE VEHICLES (%d/%d spawned)                                           "
               ANSI_RESET "\n",
               sim->vehicles_spawned, sim->total_vehicles_to_spawn);
        render_vehicles(vehicles, n);
        printf("\n");

        /* Event log */
        printf("  " ANSI_BOLD ANSI_BG_MAGENTA ANSI_WHITE
               " 📋 EVENT LOG                                                                  "
               ANSI_RESET "\n");
        render_log(sim);
        printf("\n");

        /* Footer */
        printf(ANSI_DIM "  Press Ctrl+C for graceful shutdown | "
               "Vehicles spawned: %d/%d"
               ANSI_RESET "\n",
               sim->vehicles_spawned, sim->total_vehicles_to_spawn);

        fflush(stdout);

        /* Refresh rate: ~500ms */
        usleep(500000);
    }

    return NULL;
}

/* ──────────────── Final Summary ──────────────── */
void display_final_summary(SimulationState *sim, Vehicle *vehicles, int n) {
    printf(ANSI_CLEAR ANSI_HOME);
    printf(ANSI_BOLD "\n");
    printf("  ╔══════════════════════════════════════════════════════════════╗\n");
    printf("  ║              SIMULATION COMPLETE — FINAL SUMMARY           ║\n");
    printf("  ╚══════════════════════════════════════════════════════════════╝\n\n");
    printf(ANSI_RESET);

    printf("  " ANSI_BOLD "F10 Intersection:" ANSI_RESET "\n");
    printf("    Total vehicles crossed: %d\n", sim->intersections[F10].total_crossed);
    printf("    Emergency preemptions:  %d\n", sim->intersections[F10].emergency_preemptions);

    printf("  " ANSI_BOLD "F11 Intersection:" ANSI_RESET "\n");
    printf("    Total vehicles crossed: %d\n", sim->intersections[F11].total_crossed);
    printf("    Emergency preemptions:  %d\n", sim->intersections[F11].emergency_preemptions);

    int f10_occ, f10_q, f11_occ, f11_q;
    parking_get_status(&sim->parking_lots[F10], &f10_occ, &f10_q);
    parking_get_status(&sim->parking_lots[F11], &f11_occ, &f11_q);

    printf("\n  " ANSI_BOLD "Parking Status:" ANSI_RESET "\n");
    printf("    F10: %d/%d spots, %d in queue\n", f10_occ, PARKING_SPOTS, f10_q);
    printf("    F11: %d/%d spots, %d in queue\n", f11_occ, PARKING_SPOTS, f11_q);

    printf("\n  " ANSI_BOLD "Vehicle Summary:" ANSI_RESET "\n");
    int counts[NUM_VEHICLE_TYPES] = {0};
    int crossed = 0, parked = 0;
    for (int i = 0; i < n; i++) {
        counts[vehicles[i].type]++;
        if (vehicles[i].state == VSTATE_EXITED || vehicles[i].state == VSTATE_CROSSED)
            crossed++;
    }

    for (int t = 0; t < NUM_VEHICLE_TYPES; t++) {
        printf("    %s %-10s: %d\n", vehicle_type_icon(t),
               vehicle_type_str(t), counts[t]);
    }
    printf("    Crossed: %d / %d\n", crossed, n);
    printf("\n  " ANSI_BOLD "Total Vehicles: %d" ANSI_RESET "\n", n);
    printf("  " ANSI_GREEN "✓ All resources cleaned up successfully." ANSI_RESET "\n\n");
}
