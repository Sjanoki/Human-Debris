#include <chrono>
#include <cmath>
#include <thread>

#include "core/WorldBuilder.hpp"
#include "gameplay/DockingSystem.hpp"
#include "gameplay/EconomySystem.hpp"
#include "gameplay/MiningSystem.hpp"
#include "gameplay/PlayerSessionSystem.hpp"
#include "gameplay/RadarSystem.hpp"
#include "gameplay/ThrustSystem.hpp"
#include "net/TcpServer.hpp"
#include "physics/PhysicsSystem.hpp"
#include "third_party/json.hpp"
#include "util/Logger.hpp"
#include "util/Vec2.hpp"

using json = nlohmann::json;

namespace {

nlohmann::json makeWorldSummary(orbital::core::World& world, orbital::core::Player& player) {
    json summary;
    summary["type"] = "world_summary";
    summary["player"] = {
        {"id", player.id},
        {"name", player.name},
        {"credits", player.credits},
        {"active_ship_id", player.activeShipId},
    };
    json ships = json::array();
    for (int shipId : player.ownedShipIds) {
        auto* ship = world.findShipById(shipId);
        if (!ship) {
            continue;
        }
        auto* body = world.findBodyById(ship->bodyId);
        json shipJson;
        shipJson["id"] = ship->id;
        shipJson["class"] = ship->shipClassId;
        shipJson["fuel"] = ship->fuelMass;
        shipJson["docked"] = ship->docked;
        shipJson["docked_station_id"] = ship->dockedStationId;
        if (body) {
            shipJson["position"] = {body->position.x, body->position.y};
            shipJson["velocity"] = {body->velocity.x, body->velocity.y};
        }
        ships.push_back(shipJson);
    }
    summary["ships"] = ships;
    summary["counts"] = {
        {"bodies", world.bodies.size()},
        {"ships", world.ships.size()},
        {"asteroids", world.asteroids.size()},
        {"stations", world.stations.size()},
    };
    return summary;
}

std::string bodyTypeToString(orbital::core::BodyType type) {
    switch (type) {
    case orbital::core::BodyType::Ship:
        return "Ship";
    case orbital::core::BodyType::Asteroid:
        return "Asteroid";
    case orbital::core::BodyType::Station:
        return "Station";
    }
    return "Unknown";
}

json makeWorldState(orbital::core::World& world, orbital::core::Player& player) {
    json state;
    state["type"] = "world_state";
    state["timestamp"] = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    json bodies = json::array();
    for (const auto& body : world.bodies) {
        if (!body.active) {
            continue;
        }
        json b;
        b["body_id"] = body.id;
        b["type"] = bodyTypeToString(body.type);
        b["x"] = body.position.x;
        b["y"] = body.position.y;
        b["angle"] = body.angle;
        b["vx"] = body.velocity.x;
        b["vy"] = body.velocity.y;
        bodies.push_back(b);
    }
    state["bodies"] = bodies;

    json ships = json::array();
    for (const auto& ship : world.ships) {
        json s;
        s["ship_id"] = ship.id;
        s["body_id"] = ship.bodyId;
        s["owner_player_id"] = ship.ownerPlayerId;
        s["ship_class_id"] = ship.shipClassId;
        s["fuel_mass"] = ship.fuelMass;
        s["docked"] = ship.docked;
        s["docked_station_id"] = ship.dockedStationId;
        const auto* hold = world.findCargoHoldById(ship.cargoHoldId);
        if (hold) {
            s["cargo_mass"] = hold->currentMass;
            s["cargo_capacity"] = hold->capacityMass;
        } else {
            s["cargo_mass"] = 0.0;
            s["cargo_capacity"] = 0.0;
        }
        ships.push_back(s);
    }
    state["ships"] = ships;

    json stations = json::array();
    for (const auto& station : world.stations) {
        json s;
        s["station_id"] = station.id;
        s["body_id"] = station.bodyId;
        s["name"] = "Station " + std::to_string(station.id);
        json market;
        market["basePricePerKg"] = station.market.basePricePerKg;
        market["purityMultiplier"] = station.market.purityMultiplier;
        json offers = json::array();
        for (const auto& offer : station.market.shipOffers) {
            json o;
            o["ship_class_id"] = offer.shipClassId;
            o["price"] = offer.price;
            offers.push_back(o);
        }
        market["ship_offers"] = offers;
        s["market"] = market;
        stations.push_back(s);
    }
    state["stations"] = stations;

    json playerJson;
    playerJson["id"] = player.id;
    playerJson["name"] = player.name;
    playerJson["credits"] = player.credits;
    playerJson["active_ship_id"] = player.activeShipId;
    playerJson["docked_station_id"] = player.dockedStationId;
    state["player"] = playerJson;

    json planet;
    planet["radius"] = world.planet.radius;
    planet["mu"] = world.planet.mu;
    state["planet"] = planet;

    json shipClasses = json::array();
    for (const auto& shipClass : world.shipClasses) {
        json sc;
        sc["ship_class_id"] = shipClass.id;
        sc["cargo_capacity"] = shipClass.cargoCapacity;
        sc["max_rotation_rate"] = shipClass.maxRotationRate;
        json engine;
        engine["name"] = shipClass.engine.name;
        engine["max_thrust"] = shipClass.engine.maxThrust;
        engine["fuel_use_per_second"] = shipClass.engine.fuelUsePerSecondAtFullThrust;
        sc["engine"] = engine;
        shipClasses.push_back(sc);
    }
    state["ship_classes"] = shipClasses;

    return state;
}

int spawnShipForPlayer(orbital::core::World& world, orbital::core::Player& player, const std::string& classId) {
    auto* shipClass = world.findShipClass(classId);
    if (!shipClass) {
        return -1;
    }
    orbital::core::Body body;
    body.id = world.nextBodyId++;
    body.type = orbital::core::BodyType::Ship;
    body.mass = shipClass->baseMass;
    body.colliderId = 0;
    double altitude = world.planet.radius + 400000.0 + world.nextShipId * 100.0;
    body.position = {altitude, 0.0};
    double orbitalSpeed = std::sqrt(world.planet.mu / altitude);
    body.velocity = {0.0, orbitalSpeed};
    world.bodies.push_back(body);

    orbital::core::CargoHold hold;
    hold.id = world.nextCargoHoldId++;
    hold.capacityMass = shipClass->cargoCapacity;
    hold.currentMass = 0.0;
    world.cargoHolds.push_back(hold);

    orbital::core::Ship ship;
    ship.id = world.nextShipId++;
    ship.shipClassId = shipClass->id;
    ship.bodyId = body.id;
    ship.fuelMass = 200.0;
    ship.maxFuelMass = 200.0;
    ship.cargoHoldId = hold.id;
    ship.ownerPlayerId = player.id;
    world.ships.push_back(ship);

    player.ownedShipIds.push_back(ship.id);
    if (player.activeShipId == -1) {
        player.activeShipId = ship.id;
    }

    return ship.id;
}

int spawnDockedShipForPlayer(orbital::core::World& world, orbital::core::Player& player, const std::string& classId,
                             int stationId) {
    auto* shipClass = world.findShipClass(classId);
    auto* station = world.findStationById(stationId);
    if (!shipClass || !station) {
        return -1;
    }
    auto* stationBody = world.findBodyById(station->bodyId);
    if (!stationBody) {
        return -1;
    }

    orbital::core::Body body;
    body.id = world.nextBodyId++;
    body.type = orbital::core::BodyType::Ship;
    body.mass = shipClass->baseMass;
    body.colliderId = 0;
    body.position = stationBody->position;
    body.velocity = stationBody->velocity;
    body.angle = stationBody->angle;
    body.angularVelocity = 0.0;
    world.bodies.push_back(body);

    orbital::core::CargoHold hold;
    hold.id = world.nextCargoHoldId++;
    hold.capacityMass = shipClass->cargoCapacity;
    hold.currentMass = 0.0;
    world.cargoHolds.push_back(hold);

    orbital::core::Ship ship;
    ship.id = world.nextShipId++;
    ship.shipClassId = shipClass->id;
    ship.bodyId = body.id;
    ship.fuelMass = 200.0;
    ship.maxFuelMass = 200.0;
    ship.cargoHoldId = hold.id;
    ship.ownerPlayerId = player.id;
    ship.docked = true;
    ship.dockedStationId = station->id;
    world.ships.push_back(ship);

    player.ownedShipIds.push_back(ship.id);
    if (player.activeShipId == -1) {
        player.activeShipId = ship.id;
    }

    return ship.id;
}

void sendError(orbital::net::TcpServer& server, int connectionId, const std::string& message) {
    json response;
    response["type"] = "error";
    response["message"] = message;
    server.sendMessage(connectionId, response.dump());
}

void sendActionResult(orbital::net::TcpServer& server, int connectionId, const std::string& message) {
    json response;
    response["type"] = "action_result";
    response["message"] = message;
    server.sendMessage(connectionId, response.dump());
}

} // namespace

int main() {
    ORBITAL_LOG(orbital::util::Logger::Level::Info, "Starting orbital server...");

    orbital::core::WorldBuilder builder("server/config");
    auto world = builder.build();

    orbital::net::TcpServer tcpServer;
    tcpServer.start(7777);

    orbital::physics::PhysicsSystem physicsSystem;
    orbital::gameplay::ThrustSystem thrustSystem;
    orbital::gameplay::MiningSystem miningSystem;
    orbital::gameplay::DockingSystem dockingSystem;
    orbital::gameplay::EconomySystem economySystem;
    orbital::gameplay::RadarSystem radarSystem;
    orbital::gameplay::PlayerSessionSystem sessionSystem;

    bool running = true;
    auto lastTick = std::chrono::steady_clock::now();
    double accumulator = 0.0;
    double worldStateAccumulator = 0.0;
    const double worldStateInterval = 0.25;

    while (running) {
        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double>(now - lastTick).count();
        lastTick = now;
        accumulator += delta;
        worldStateAccumulator += delta;

        std::vector<orbital::net::NetworkMessage> messages;
        tcpServer.pollMessages(messages);
        for (const auto& message : messages) {
            if (message.text.empty()) {
                continue;
            }
            json data;
            try {
                data = json::parse(message.text);
            } catch (const std::exception& e) {
                sendError(tcpServer, message.connectionId, std::string("Invalid JSON: ") + e.what());
                continue;
            }
            std::string type = data.value("type", "");
            if (type == "login") {
                std::string name = data.value("player_name", "Guest");
                auto* player = sessionSystem.login(world, name, message.connectionId);
                if (player) {
                    if (player->ownedShipIds.empty()) {
                        int spawnedId =
                            spawnDockedShipForPlayer(world, *player, world.simulation.defaultShipClassId,
                                                     world.simulation.defaultSpawnStationId);
                        if (spawnedId >= 0) {
                            ORBITAL_LOG(orbital::util::Logger::Level::Info, "Spawned default ship ", spawnedId,
                                        " for player ", player->name);
                        } else {
                            ORBITAL_LOG(orbital::util::Logger::Level::Warning,
                                        "Failed to spawn default ship for player ", player->name);
                        }
                    } else {
                        auto* activeShip = world.findShipById(player->activeShipId);
                        if (!activeShip && !player->ownedShipIds.empty()) {
                            player->activeShipId = player->ownedShipIds.front();
                        }
                    }
                    tcpServer.sendMessage(message.connectionId, makeWorldSummary(world, *player).dump());
                }
            } else if (type == "request_world_summary") {
                auto* player = world.findPlayerByConnection(message.connectionId);
                if (player) {
                    tcpServer.sendMessage(message.connectionId, makeWorldSummary(world, *player).dump());
                }
            } else if (type == "spawn_test_ship") {
                auto* player = world.findPlayerByConnection(message.connectionId);
                if (!player) {
                    continue;
                }
                std::string classId = data.value("ship_class_id", "SCOUT");
                int shipId = spawnShipForPlayer(world, *player, classId);
                if (shipId >= 0) {
                    sendActionResult(tcpServer, message.connectionId, "Spawned ship " + std::to_string(shipId));
                } else {
                    sendError(tcpServer, message.connectionId, "Unknown ship class");
                }
            } else if (type == "thrust") {
                orbital::core::ThrustCommand cmd;
                cmd.shipId = data.value("ship_id", -1);
                cmd.throttle = data.value("throttle", 0.0);
                cmd.duration = data.value("duration", 0.0);
                world.commandQueues.thrustCommands.push_back(cmd);
            } else if (type == "rotate_ship") {
                int shipId = data.value("ship_id", -1);
                auto* ship = world.findShipById(shipId);
                if (ship) {
                    auto* body = world.findBodyById(ship->bodyId);
                    auto* shipClass = world.findShipClass(ship->shipClassId);
                    if (body && shipClass) {
                        std::string direction = data.value("direction", "left");
                        double duration = data.value("duration", 0.0);
                        double sign = direction == "right" ? 1.0 : -1.0;
                        body->angle += sign * shipClass->maxRotationRate * duration;
                    }
                }
            } else if (type == "mine") {
                orbital::core::MineCommand cmd;
                cmd.shipId = data.value("ship_id", -1);
                cmd.asteroidId = data.value("asteroid_id", -1);
                cmd.duration = data.value("duration", 0.0);
                world.commandQueues.mineCommands.push_back(cmd);
            } else if (type == "dock") {
                orbital::core::DockCommand cmd;
                cmd.shipId = data.value("ship_id", -1);
                cmd.stationId = data.value("station_id", -1);
                world.commandQueues.dockCommands.push_back(cmd);
            } else if (type == "undock") {
                orbital::core::DockCommand cmd;
                cmd.shipId = data.value("ship_id", -1);
                world.commandQueues.undockCommands.push_back(cmd);
            } else if (type == "sell_ore") {
                orbital::core::SellOreCommand cmd;
                cmd.shipId = data.value("ship_id", -1);
                cmd.stationId = data.value("station_id", -1);
                world.commandQueues.sellCommands.push_back(cmd);
            } else if (type == "buy_ship") {
                orbital::core::BuyShipCommand cmd;
                cmd.stationId = data.value("station_id", -1);
                cmd.shipClassId = data.value("ship_class_id", "SCOUT");
                world.commandQueues.buyCommands.push_back(cmd);
            } else if (type == "radar_scan") {
                int shipId = data.value("ship_id", -1);
                auto contacts = radarSystem.scan(world, shipId);
                json payload;
                payload["type"] = "radar_result";
                json array = json::array();
                for (const auto& contact : contacts) {
                    json entry;
                    entry["body_id"] = contact.bodyId;
                    entry["type"] = static_cast<int>(contact.type);
                    entry["distance"] = contact.distance;
                    entry["bearing"] = contact.bearing;
                    array.push_back(entry);
                }
                payload["contacts"] = array;
                tcpServer.sendMessage(message.connectionId, payload.dump());
            } else if (type == "switch_ship") {
                auto* player = world.findPlayerByConnection(message.connectionId);
                if (!player) {
                    continue;
                }
                int shipId = data.value("new_ship_id", -1);
                bool owns = false;
                for (int ownedId : player->ownedShipIds) {
                    if (ownedId == shipId) {
                        owns = true;
                        break;
                    }
                }
                if (owns) {
                    player->activeShipId = shipId;
                    sendActionResult(tcpServer, message.connectionId, "Switched to ship " + std::to_string(shipId));
                }
            } else if (type == "control_state") {
                auto* player = world.findPlayerByConnection(message.connectionId);
                if (!player) {
                    continue;
                }
                int shipId = data.value("ship_id", -1);
                auto* ship = world.findShipById(shipId);
                if (!ship || ship->ownerPlayerId != player->id) {
                    continue;
                }
                ship->controlState.thrust = data.value("thrust", ship->controlState.thrust);
                ship->controlState.turnLeft = data.value("turn_left", ship->controlState.turnLeft);
                ship->controlState.turnRight = data.value("turn_right", ship->controlState.turnRight);
                ship->controlState.mine = data.value("mine", ship->controlState.mine);
            } else if (type == "subscribe_world_state") {
                auto* player = world.findPlayerByConnection(message.connectionId);
                if (!player) {
                    continue;
                }
                bool enabled = data.value("enabled", true);
                player->worldStateSubscribed = enabled;
                sendActionResult(tcpServer, message.connectionId,
                                enabled ? "World state streaming enabled." : "World state streaming disabled.");
            } else if (type == "logout") {
                sessionSystem.logout(world, message.connectionId);
            }
        }

        const double dt = world.simulation.timeStep;
        while (accumulator >= dt) {
            thrustSystem.process(world);
            physicsSystem.step(world, dt);
            miningSystem.process(world);
            dockingSystem.process(world);
            dockingSystem.processUndock(world);
            economySystem.processSell(world);
            economySystem.processBuy(world);
            accumulator -= dt;
        }

        if (worldStateAccumulator >= worldStateInterval) {
            worldStateAccumulator = 0.0;
            for (auto& player : world.players) {
                if (!player.online || !player.worldStateSubscribed || player.connectionId < 0) {
                    continue;
                }
                tcpServer.sendMessage(player.connectionId, makeWorldState(world, player).dump());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    tcpServer.stop();
    return 0;
}
