/*
 * gui_display.cpp - SFML Graphical Display for Traffic Simulation
 * Compiled as C++, linked with C simulation core via extern "C".
 */

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <string>
#include <cmath>
#include <sstream>
#include <cstring>

extern "C" {
#include "common.h"
#include "parking.h"
#include "gui_display.h"
}

/* ── Color Palette ── */
static const sf::Color BG_COLOR(20, 20, 35);
static const sf::Color ROAD_COLOR(60, 60, 75);
static const sf::Color ROAD_LINE(180, 180, 60);
static const sf::Color GRASS_COLOR(30, 80, 40);
static const sf::Color PANEL_BG(30, 30, 50, 230);
static const sf::Color PANEL_BORDER(80, 80, 140);
static const sf::Color TEXT_WHITE(230, 230, 240);
static const sf::Color TEXT_DIM(140, 140, 160);
static const sf::Color RED_LIGHT(220, 40, 40);
static const sf::Color GREEN_LIGHT(40, 220, 60);
static const sf::Color YELLOW_LIGHT(240, 220, 40);
static const sf::Color PARK_BLUE(50, 120, 220);
static const sf::Color PARK_EMPTY(60, 60, 80);
static const sf::Color EMERGENCY_GLOW(255, 40, 40, 80);

/* Vehicle colors by type */
static sf::Color vehicle_color(VehicleType t) {
    switch (t) {
        case AMBULANCE: return sf::Color(255, 60, 60);
        case FIRETRUCK: return sf::Color(255, 120, 30);
        case BUS:       return sf::Color(255, 220, 40);
        case CAR:       return sf::Color(80, 160, 255);
        case BIKE:      return sf::Color(80, 230, 160);
        case TRACTOR:   return sf::Color(180, 130, 80);
        default:        return sf::Color::White;
    }
}

static sf::Color light_color(LightState l) {
    switch (l) {
        case RED:    return RED_LIGHT;
        case GREEN:  return GREEN_LIGHT;
        case YELLOW: return YELLOW_LIGHT;
        default:     return sf::Color(60, 60, 60);
    }
}

/* ── Draw a rounded rectangle ── */
static void drawRoundedRect(sf::RenderWindow &win, float x, float y,
                            float w, float h, float r, sf::Color fill,
                            sf::Color outline = sf::Color::Transparent, float thick = 0) {
    sf::RectangleShape rect(sf::Vector2f(w - 2*r, h));
    rect.setPosition(x + r, y);
    rect.setFillColor(fill);
    win.draw(rect);

    sf::RectangleShape rect2(sf::Vector2f(w, h - 2*r));
    rect2.setPosition(x, y + r);
    rect2.setFillColor(fill);
    win.draw(rect2);

    float positions[][2] = {{x+r, y+r}, {x+w-r, y+r}, {x+r, y+h-r}, {x+w-r, y+h-r}};
    for (int i = 0; i < 4; i++) {
        sf::CircleShape corner(r);
        corner.setOrigin(r, r);
        corner.setPosition(positions[i][0], positions[i][1]);
        corner.setFillColor(fill);
        win.draw(corner);
    }

    if (thick > 0 && outline != sf::Color::Transparent) {
        sf::RectangleShape border(sf::Vector2f(w, h));
        border.setPosition(x, y);
        border.setFillColor(sf::Color::Transparent);
        border.setOutlineColor(outline);
        border.setOutlineThickness(thick);
        win.draw(border);
    }
}

/* ── Draw a traffic light (3 circles) ── */
static void drawTrafficLight(sf::RenderWindow &win, float x, float y,
                             LightState state) {
    drawRoundedRect(win, x, y, 24, 70, 5, sf::Color(30, 30, 30));

    sf::Color colors[3] = {
        (state == RED)    ? RED_LIGHT    : sf::Color(60, 20, 20),
        (state == YELLOW) ? YELLOW_LIGHT : sf::Color(60, 55, 20),
        (state == GREEN)  ? GREEN_LIGHT  : sf::Color(20, 60, 25)
    };

    for (int i = 0; i < 3; i++) {
        sf::CircleShape bulb(8);
        bulb.setOrigin(8, 8);
        bulb.setPosition(x + 12, y + 12 + i * 22);
        bulb.setFillColor(colors[i]);
        if ((i == 0 && state == RED) || (i == 1 && state == YELLOW) ||
            (i == 2 && state == GREEN)) {
            /* Glow effect */
            sf::CircleShape glow(14);
            glow.setOrigin(14, 14);
            glow.setPosition(x + 12, y + 12 + i * 22);
            glow.setFillColor(sf::Color(colors[i].r, colors[i].g, colors[i].b, 60));
            win.draw(glow);
        }
        win.draw(bulb);
    }
}

/* ── Draw one intersection ── */
static void drawIntersection(sf::RenderWindow &win, sf::Font &font,
                             float cx, float cy, IntersectionState *inter,
                             ParkingLot *lot, const char *label) {
    float roadW = 80, roadLen = 140;

    /* Emergency glow */
    if (inter->emergency_active) {
        sf::CircleShape glow(100);
        glow.setOrigin(100, 100);
        glow.setPosition(cx, cy);
        glow.setFillColor(EMERGENCY_GLOW);
        win.draw(glow);
    }

    /* Roads */
    sf::RectangleShape hRoad(sf::Vector2f(roadLen * 2 + roadW, roadW));
    hRoad.setPosition(cx - roadLen - roadW/2, cy - roadW/2);
    hRoad.setFillColor(ROAD_COLOR);
    win.draw(hRoad);

    sf::RectangleShape vRoad(sf::Vector2f(roadW, roadLen * 2 + roadW));
    vRoad.setPosition(cx - roadW/2, cy - roadLen - roadW/2);
    vRoad.setFillColor(ROAD_COLOR);
    win.draw(vRoad);

    /* Center intersection box */
    sf::RectangleShape center(sf::Vector2f(roadW, roadW));
    center.setPosition(cx - roadW/2, cy - roadW/2);
    center.setFillColor(sf::Color(70, 70, 85));
    win.draw(center);

    /* Dashed center lines */
    for (int i = 0; i < 6; i++) {
        sf::RectangleShape dash(sf::Vector2f(12, 2));
        dash.setFillColor(ROAD_LINE);
        /* Horizontal */
        dash.setPosition(cx - roadLen - roadW/2 + i * 25, cy);
        win.draw(dash);
        dash.setPosition(cx + roadW/2 + 10 + i * 25, cy);
        win.draw(dash);
    }
    for (int i = 0; i < 6; i++) {
        sf::RectangleShape dash(sf::Vector2f(2, 12));
        dash.setFillColor(ROAD_LINE);
        dash.setPosition(cx, cy - roadLen - roadW/2 + i * 25);
        win.draw(dash);
        dash.setPosition(cx, cy + roadW/2 + 10 + i * 25);
        win.draw(dash);
    }

    /* Traffic lights at each direction */
    drawTrafficLight(win, cx - 55, cy - roadLen - roadW/2 - 30, inter->lights[NORTH]);
    drawTrafficLight(win, cx + 32, cy + roadLen + roadW/2 - 40, inter->lights[SOUTH]);
    drawTrafficLight(win, cx + roadLen + roadW/2 - 5, cy - 55, inter->lights[EAST]);
    drawTrafficLight(win, cx - roadLen - roadW/2 - 20, cy + 32, inter->lights[WEST]);

    /* Label */
    sf::Text lbl(label, font, 20);
    lbl.setFillColor(TEXT_WHITE);
    lbl.setStyle(sf::Text::Bold);
    lbl.setOrigin(lbl.getLocalBounds().width / 2, lbl.getLocalBounds().height / 2);
    lbl.setPosition(cx, cy);
    win.draw(lbl);

    /* Direction labels */
    const char *dirs[] = {"N", "S", "E", "W"};
    float dpos[][2] = {{cx, cy - roadLen - 20}, {cx, cy + roadLen + 10},
                       {cx + roadLen + 15, cy}, {cx - roadLen - 22, cy}};
    for (int i = 0; i < 4; i++) {
        sf::Text d(dirs[i], font, 14);
        d.setFillColor(TEXT_DIM);
        d.setPosition(dpos[i][0], dpos[i][1]);
        win.draw(d);
    }

    /* Parking lot panel below intersection */
    float px = cx - 90, py = cy + roadLen + 70;
    drawRoundedRect(win, px, py, 180, 60, 6, PANEL_BG, PANEL_BORDER, 1);

    sf::Text ptitle("PARKING", font, 11);
    ptitle.setFillColor(PARK_BLUE);
    ptitle.setStyle(sf::Text::Bold);
    ptitle.setPosition(px + 8, py + 4);
    win.draw(ptitle);

    int occ, inq;
    parking_get_status(lot, &occ, &inq);

    /* Parking spots bar */
    for (int i = 0; i < PARKING_SPOTS; i++) {
        sf::RectangleShape spot(sf::Vector2f(13, 10));
        spot.setPosition(px + 8 + i * 16, py + 22);
        spot.setFillColor(i < occ ? PARK_BLUE : PARK_EMPTY);
        spot.setOutlineColor(sf::Color(40, 40, 60));
        spot.setOutlineThickness(1);
        win.draw(spot);
    }

    std::ostringstream ss;
    ss << occ << "/" << PARKING_SPOTS << " spots   Q: " << inq << "/" << PARKING_QUEUE_SIZE;
    sf::Text pinfo(ss.str(), font, 10);
    pinfo.setFillColor(TEXT_DIM);
    pinfo.setPosition(px + 8, py + 40);
    win.draw(pinfo);
}

/* ── Draw vehicle icons in the vehicle table ── */
static void drawVehicleRow(sf::RenderWindow &win, sf::Font &font,
                           float x, float y, Vehicle *v) {
    /* Color indicator */
    sf::RectangleShape indicator(sf::Vector2f(6, 16));
    indicator.setPosition(x, y + 2);
    indicator.setFillColor(vehicle_color(v->type));
    win.draw(indicator);

    /* ID */
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02d", v->id);
    sf::Text idText(buf, font, 12);
    idText.setFillColor(TEXT_WHITE);
    idText.setPosition(x + 12, y);
    win.draw(idText);

    /* Type */
    sf::Text typeText(vehicle_type_str(v->type), font, 12);
    typeText.setFillColor(vehicle_color(v->type));
    typeText.setPosition(x + 50, y);
    win.draw(typeText);

    /* Location */
    std::string loc = std::string(intersection_str(v->origin_intersection)) +
                      "-" + direction_short(v->origin_direction);
    sf::Text locText(loc, font, 12);
    locText.setFillColor(TEXT_DIM);
    locText.setPosition(x + 140, y);
    win.draw(locText);

    /* State */
    sf::Color stateCol;
    switch (v->state) {
        case VSTATE_CROSSING:   stateCol = GREEN_LIGHT; break;
        case VSTATE_PARKED:     stateCol = PARK_BLUE; break;
        case VSTATE_WAITING:    stateCol = YELLOW_LIGHT; break;
        case VSTATE_QUEUE_PARK: stateCol = sf::Color(200, 100, 220); break;
        case VSTATE_EXITED:     stateCol = sf::Color(80, 80, 100); break;
        default:                stateCol = TEXT_DIM; break;
    }
    sf::Text stateText(state_str(v->state), font, 12);
    stateText.setFillColor(stateCol);
    stateText.setPosition(x + 220, y);
    win.draw(stateText);
}

/* ── Main GUI thread function ── */
extern "C" void* gui_display_thread(void *arg) {
    GUIDisplayArgs *dargs = (GUIDisplayArgs *)arg;
    SimulationState *sim = dargs->sim;
    Vehicle *vehicles = dargs->vehicles;
    int n = dargs->num_vehicles;

    /* Create window */
    sf::RenderWindow window(sf::VideoMode(1200, 800),
                            "Traffic Intersection Simulator - F10 & F11",
                            sf::Style::Close | sf::Style::Titlebar);
    window.setFramerateLimit(30);

    /* Load font */
    sf::Font font;
    bool fontLoaded = false;
    const char *fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        NULL
    };
    for (int i = 0; fontPaths[i]; i++) {
        if (font.loadFromFile(fontPaths[i])) { fontLoaded = true; break; }
    }
    if (!fontLoaded) {
        fprintf(stderr, "Warning: Could not load any system font for GUI\n");
        return NULL;
    }

    sf::Clock clock;

    while (window.isOpen() && sim->simulation_running && !g_shutdown_flag) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
                return NULL;
            }
        }

        float animTime = clock.getElapsedTime().asSeconds();
        (void)animTime;
        window.clear(BG_COLOR);

        /* ── Title bar ── */
        sf::RectangleShape titleBar(sf::Vector2f(1200, 50));
        titleBar.setFillColor(sf::Color(25, 25, 55));
        window.draw(titleBar);

        sf::Text title("TRAFFIC INTERSECTION SIMULATOR", font, 22);
        title.setFillColor(sf::Color(100, 180, 255));
        title.setStyle(sf::Text::Bold);
        title.setPosition(30, 12);
        window.draw(title);

        /* Spawned counter */
        std::ostringstream ctr;
        ctr << "Vehicles: " << sim->vehicles_spawned << "/" << sim->total_vehicles_to_spawn;
        sf::Text counter(ctr.str(), font, 14);
        counter.setFillColor(TEXT_DIM);
        counter.setPosition(1020, 18);
        window.draw(counter);

        /* ── Draw connecting road between intersections ── */
        sf::RectangleShape connRoad(sf::Vector2f(200, 40));
        connRoad.setPosition(400, 280);
        connRoad.setFillColor(ROAD_COLOR);
        window.draw(connRoad);

        /* Dashed line on connecting road */
        for (int i = 0; i < 8; i++) {
            sf::RectangleShape dash(sf::Vector2f(12, 2));
            dash.setFillColor(ROAD_LINE);
            dash.setPosition(410 + i * 24, 300);
            window.draw(dash);
        }

        /* Arrow indicators */
        sf::Text arrow("<-->", font, 16);
        arrow.setFillColor(sf::Color(120, 120, 160));
        arrow.setPosition(480, 260);
        window.draw(arrow);

        /* ── Draw intersections ── */
        drawIntersection(window, font, 300, 300,
                         &sim->intersections[F10], &sim->parking_lots[F10], "F10");
        drawIntersection(window, font, 700, 300,
                         &sim->intersections[F11], &sim->parking_lots[F11], "F11");

        /* ── Stats panel ── */
        drawRoundedRect(window, 920, 70, 260, 140, 8, PANEL_BG, PANEL_BORDER, 1);

        sf::Text statsTitle("STATISTICS", font, 13);
        statsTitle.setFillColor(sf::Color(100, 180, 255));
        statsTitle.setStyle(sf::Text::Bold);
        statsTitle.setPosition(935, 78);
        window.draw(statsTitle);

        std::ostringstream s1, s2, s3, s4;
        s1 << "F10 Crossed: " << sim->intersections[F10].total_crossed;
        s2 << "F11 Crossed: " << sim->intersections[F11].total_crossed;
        s3 << "F10 Emergencies: " << sim->intersections[F10].emergency_preemptions;
        s4 << "F11 Emergencies: " << sim->intersections[F11].emergency_preemptions;

        sf::Text st[] = {
            sf::Text(s1.str(), font, 12), sf::Text(s2.str(), font, 12),
            sf::Text(s3.str(), font, 12), sf::Text(s4.str(), font, 12)
        };
        for (int i = 0; i < 4; i++) {
            st[i].setFillColor(i < 2 ? TEXT_WHITE : sf::Color(255, 120, 100));
            st[i].setPosition(935, 100 + i * 22);
            window.draw(st[i]);
        }

        /* ── Vehicle table panel ── */
        float panelY = 530;
        drawRoundedRect(window, 20, panelY, 1160, 250, 8, PANEL_BG, PANEL_BORDER, 1);

        sf::Text vtitle("ACTIVE VEHICLES", font, 14);
        vtitle.setFillColor(sf::Color(100, 180, 255));
        vtitle.setStyle(sf::Text::Bold);
        vtitle.setPosition(35, panelY + 8);
        window.draw(vtitle);

        /* Column headers */
        const char *headers[] = {"ID", "Type", "Location", "State"};
        float hx[] = {47, 85, 175, 255};
        for (int i = 0; i < 4; i++) {
            sf::Text h(headers[i], font, 10);
            h.setFillColor(TEXT_DIM);
            h.setPosition(hx[i], panelY + 30);
            window.draw(h);
        }

        /* Vehicle rows - 3 columns layout */
        int col = 0, row = 0;
        for (int i = 0; i < n; i++) {
            if (vehicles[i].state == VSTATE_SPAWNED && !vehicles[i].active) continue;

            float vx = 35 + col * 380;
            float vy = panelY + 48 + row * 20;

            if (vy > panelY + 230) break;

            drawVehicleRow(window, font, vx, vy, &vehicles[i]);

            col++;
            if (col >= 3) { col = 0; row++; }
        }

        /* ── Event log panel ── */
        drawRoundedRect(window, 20, panelY - 60, 880, 55, 6, PANEL_BG, PANEL_BORDER, 1);

        sf::Text logTitle("EVENT LOG", font, 11);
        logTitle.setFillColor(sf::Color(200, 100, 220));
        logTitle.setStyle(sf::Text::Bold);
        logTitle.setPosition(35, panelY - 55);
        window.draw(logTitle);

        /* Show last 2 log entries */
        pthread_mutex_lock(&sim->log_mutex);
        int logCount = sim->log_count < 2 ? sim->log_count : 2;
        for (int i = 0; i < logCount; i++) {
            int idx = (sim->log_head - logCount + i + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
            sf::Text logText(sim->log_entries[idx].message, font, 10);
            logText.setFillColor(TEXT_DIM);
            logText.setPosition(35, panelY - 40 + i * 16);
            window.draw(logText);
        }
        pthread_mutex_unlock(&sim->log_mutex);

        /* ── Legend ── */
        const char *typeNames[] = {"Ambulance", "Firetruck", "Bus", "Car", "Bike", "Tractor"};
        for (int i = 0; i < NUM_VEHICLE_TYPES; i++) {
            sf::RectangleShape swatch(sf::Vector2f(10, 10));
            swatch.setPosition(935 + (i % 3) * 90, 220 + (i / 3) * 20);
            swatch.setFillColor(vehicle_color((VehicleType)i));
            window.draw(swatch);

            sf::Text name(typeNames[i], font, 10);
            name.setFillColor(TEXT_DIM);
            name.setPosition(950 + (i % 3) * 90, 218 + (i / 3) * 20);
            window.draw(name);
        }

        window.display();
    }

    window.close();
    return NULL;
}

