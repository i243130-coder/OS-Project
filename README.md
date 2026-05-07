# 🚦 Traffic Intersection Simulator — F10 & F11

A multi-process, multi-threaded traffic intersection simulator built in C/C++ demonstrating core Operating Systems concepts: **process creation (fork)**, **inter-process communication (pipes)**, **concurrency (pthreads)**, **synchronization (semaphores & mutexes)**, and **signal handling (SIGINT)**.

---

## 📋 Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Prerequisites](#prerequisites)
- [Build & Run](#build--run)
- [Project Structure](#project-structure)
- [Module Descriptions](#module-descriptions)
- [OS Concepts Demonstrated](#os-concepts-demonstrated)
- [Configuration](#configuration)

---

## Overview

The simulator models **two neighbouring intersections (F10 and F11)** with:

- **15 concurrent vehicles** (pthreads), each belonging to one of six categories
- **Two traffic controller processes** (forked children) coordinating via **bidirectional pipes**
- **Priority handling** for emergency vehicles (Ambulance, Firetruck)
- **Parking Lot System** at each intersection with **dual semaphores** (10 spots + bounded queue)
- **Real-time visualization** in both the terminal (ANSI) and an **SFML graphical window**

---

## Architecture

```
┌──────────────────────────────────────────────┐
│           Parent Process (main)              │
│  ┌────────────────────────────────────────┐  │
│  │  Vehicle Threads   (pthread × 15)      │  │
│  │  Console Display   (pthread × 1)       │  │
│  │  SFML GUI Display  (pthread × 1)       │  │
│  └────────────────────────────────────────┘  │
└───────┬────────────────────┬─────────────────┘
        │ pipe               │ pipe
┌───────▼────────┐  ┌───────▼────────┐
│  F10 Controller│  │  F11 Controller│
│  (child proc)  │◄─►  (child proc)  │
└────────────────┘  └────────────────┘
        pipe (bidirectional)
```

**Shared Memory Layout** (`mmap MAP_SHARED`):

```
SimulationState
├── intersections[2]   (process-shared mutex + condition variables)
├── parking_lots[2]    (unnamed semaphores with pshared=1)
├── log_entries[]      (circular buffer for event log)
└── control flags      (simulation_running, vehicles_spawned, etc.)
```

---

## Features

### Vehicle Simulation
| Category   | Priority | Can Park? | Special Behaviour                    |
|------------|----------|-----------|--------------------------------------|
| 🚑 Ambulance | HIGH     | No        | Triggers emergency preemption        |
| 🚒 Firetruck | HIGH     | No        | Triggers emergency preemption        |
| 🚌 Bus       | MEDIUM   | Yes       | Medium priority at intersections     |
| 🚗 Car       | LOW      | Yes       | Normal traffic                       |
| 🏍 Bike      | LOW      | Yes       | Normal traffic                       |
| 🚜 Tractor   | LOW      | Yes       | Normal traffic                       |

### Traffic Control
- **Phase cycling**: North-South ↔ East-West with yellow transitions
- **Emergency preemption**: Emergency vehicles override traffic phases; the controller clears the intersection and gives immediate green
- **Inter-intersection coordination**: When an emergency vehicle travels from F10 to F11, F10's controller notifies F11 via pipe so it can prepare

### Parking Lot (Dual Semaphore)
- **`spots_sem`** (init = 10): Tracks available parking spaces
- **`queue_sem`** (init = 5): Tracks available waiting queue slots
- Vehicles use **`sem_trywait`** (non-blocking) to enter the queue — they **never block the intersection**
- Once in the queue, vehicles use **`sem_wait`** (blocking) to wait for a spot

### Graceful Shutdown
- Catches **SIGINT (Ctrl+C)**
- Stops spawning new vehicles
- Joins all vehicle threads
- Terminates controller processes
- Destroys all semaphores, mutexes, and shared memory

---

## Prerequisites

```bash
# Build tools
sudo apt install gcc g++ make

# SFML library (for graphical display)
sudo apt install libsfml-dev
```

---

## Build & Run

```bash
# Compile everything
make

# Run with default 15 vehicles
./traffic_sim

# Run with custom vehicle count (1–50)
./traffic_sim 20

# Clean build artifacts
make clean

# Build with debug symbols
make debug
```

### Makefile Targets

| Target    | Description                              |
|-----------|------------------------------------------|
| `make`    | Build the simulator                      |
| `make run`| Build and run with 15 vehicles           |
| `make run20` | Build and run with 20 vehicles        |
| `make clean` | Remove all build artifacts            |
| `make debug` | Build with `-g -O0` for GDB debugging |

---

## Project Structure

```
OS Project/
├── common.h           # Shared types, constants, enums, macros
├── vehicle.h          # Vehicle thread interface
├── vehicle.c          # Vehicle lifecycle (spawn → park → cross → exit)
├── parking.h          # Parking lot interface
├── parking.c          # Dual-semaphore parking implementation
├── controller.h       # Traffic controller interface
├── controller.c       # Controller process (phase cycling + emergency)
├── display.h          # Console display interface
├── display.c          # Terminal visualization (ANSI escape codes)
├── gui_display.h      # SFML GUI display interface
├── gui_display.cpp    # SFML graphical window rendering
├── main.c             # Entry point: fork, pipes, threads, cleanup
├── Makefile           # Build system
└── README.md          # This file
```

---

## Module Descriptions

### `main.c` — Simulation Orchestrator
- Allocates shared memory via `mmap(MAP_SHARED | MAP_ANONYMOUS)`
- Creates bidirectional pipes for IPC
- Forks F10 and F11 controller child processes
- Spawns 15 vehicle threads at random intervals
- Handles SIGINT for graceful shutdown
- Joins all threads, waits for children, deallocates resources

### `vehicle.c` — Vehicle Threads
- Each vehicle is a `pthread` with metadata: id, type, origin, destination, priority, arrival_time
- Lifecycle: Spawn → (Optional) Park → Wait for Green → Cross → (Optional) Travel to Other Intersection → Exit
- Emergency vehicles request preemption and notify controllers via pipe
- Non-conflicting movement logic ensures safe concurrent crossing

### `parking.c` — Parking Lot System
- Two semaphores per lot: `spots_sem` (capacity 10) and `queue_sem` (capacity 5)
- `parking_try_enter_queue()` uses `sem_trywait` — non-blocking, intersection-safe
- `parking_wait_for_spot()` uses `sem_timedwait` — blocking but off the intersection
- All counter updates protected by `pthread_mutex` (process-shared)

### `controller.c` — Traffic Controllers
- Runs as a **separate child process** (forked from parent)
- Cycles traffic phases: NS Green → Yellow → EW Green → Yellow → repeat
- Monitors pipes for emergency messages from other controller and vehicles
- On emergency: sets all lights red, waits for intersection to clear, gives green to emergency direction
- Forwards emergency alerts between controllers for coordination

### `gui_display.cpp` — SFML Graphical Display
- Renders both intersections with roads, traffic lights (with glow effects), and direction labels
- Shows parking lot occupancy bars and queue status
- Displays vehicle table with color-coded types and states
- Statistics panel with crossing counts and emergency preemptions
- Event log with latest simulation events
- Emergency glow animation around affected intersection

### `display.c` — Console Display
- Terminal-based dashboard using ANSI escape codes
- ASCII intersection map with colored traffic lights
- Parking progress bars and vehicle status table
- Scrolling event log with timestamps

---

## OS Concepts Demonstrated

| Concept                    | Implementation                                                |
|----------------------------|---------------------------------------------------------------|
| **Process Creation**       | `fork()` creates F10 and F11 controller child processes       |
| **Inter-Process Comm.**    | Bidirectional `pipe()` between controllers for emergency sync |
| **Shared Memory**          | `mmap(MAP_SHARED \| MAP_ANONYMOUS)` for simulation state      |
| **Threads**                | `pthread_create()` for 15 vehicle threads + display threads   |
| **Mutexes**                | `pthread_mutex` with `PTHREAD_PROCESS_SHARED` attribute       |
| **Condition Variables**    | `pthread_cond` for traffic light wait/signal                  |
| **Semaphores**             | `sem_init(pshared=1)` for parking spots and queue slots       |
| **Signal Handling**        | `sigaction(SIGINT)` for graceful Ctrl+C shutdown              |
| **Non-blocking I/O**       | `sem_trywait()` to avoid intersection blocking                |
| **select() / fd_set**      | Non-blocking pipe reads in controller with `select()`         |
| **Graceful Cleanup**       | Thread joining, process waiting, resource deallocation        |

---

## Configuration

Key constants in `common.h`:

| Constant             | Default | Description                          |
|----------------------|---------|--------------------------------------|
| `MAX_VEHICLES`       | 15      | Default number of vehicle threads    |
| `PARKING_SPOTS`      | 10      | Fixed parking capacity per lot       |
| `PARKING_QUEUE_SIZE` | 5       | Bounded waiting queue size           |
| `PHASE_DURATION_SEC` | 3       | Duration of each traffic phase       |
| `CROSSING_TIME_SEC`  | 2       | Time to cross an intersection        |
| `SPAWN_INTERVAL_MIN` | 1       | Minimum spawn interval (seconds)     |
| `SPAWN_INTERVAL_MAX` | 3       | Maximum spawn interval (seconds)     |
| `PARK_DURATION_MIN`  | 2       | Minimum parking duration (seconds)   |
| `PARK_DURATION_MAX`  | 5       | Maximum parking duration (seconds)   |

---
# OS-Project
