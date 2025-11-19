#include "gameplay/MiningSystem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace orbital::gameplay {

using orbital::core::Asteroid;
using orbital::core::Body;
using orbital::core::CargoHold;
using orbital::core::CargoItem;
using orbital::core::World;

namespace {

void extractOre(core::World& world, core::Ship& ship, core::Asteroid& asteroid, double durationSeconds) {
    if (durationSeconds <= 0.0) {
        return;
    }
    auto* shipBody = world.findBodyById(ship.bodyId);
    auto* asteroidBody = world.findBodyById(asteroid.bodyId);
    if (!shipBody || !asteroidBody) {
        return;
    }
    double distance = std::hypot(shipBody->position.x - asteroidBody->position.x, shipBody->position.y - asteroidBody->position.y);
    if (distance > 50.0) {
        return;
    }
    auto* hold = world.findCargoHoldById(ship.cargoHoldId);
    if (!hold) {
        return;
    }
    double remainingCapacity = hold->capacityMass - hold->currentMass;
    if (remainingCapacity <= 0.0) {
        return;
    }
    double rate = 5.0; // kg per second
    double extract = std::min({durationSeconds * rate, asteroid.remainingMass, remainingCapacity});
    if (extract <= 0.0) {
        return;
    }
    asteroid.remainingMass -= extract;
    asteroidBody->mass -= extract;
    hold->currentMass += extract;
    bool foundOre = false;
    for (auto& item : hold->items) {
        if (item.type == core::CargoType::Ore && std::abs(item.purity - asteroid.purity) < 0.01) {
            item.mass += extract;
            foundOre = true;
            break;
        }
    }
    if (!foundOre) {
        core::CargoItem item;
        item.type = core::CargoType::Ore;
        item.mass = extract;
        item.purity = asteroid.purity;
        hold->items.push_back(item);
    }
    if (asteroid.remainingMass <= 1.0) {
        asteroidBody->active = false;
    }
}

} // namespace

void MiningSystem::process(World& world) {
    auto commands = world.commandQueues.mineCommands;
    world.commandQueues.mineCommands.clear();

    for (const auto& cmd : commands) {
        auto* ship = world.findShipById(cmd.shipId);
        if (!ship) {
            continue;
        }
        auto* asteroid = world.findAsteroidByBody(cmd.asteroidId);
        if (!asteroid) {
            continue;
        }
        extractOre(world, *ship, *asteroid, cmd.duration);
    }

    double dt = world.simulation.timeStep;
    for (auto& ship : world.ships) {
        if (!ship.controlState.mine || ship.docked) {
            continue;
        }
        auto* body = world.findBodyById(ship.bodyId);
        if (!body) {
            continue;
        }
        core::Asteroid* nearest = nullptr;
        double bestDistance = std::numeric_limits<double>::max();
        for (auto& asteroid : world.asteroids) {
            auto* asteroidBody = world.findBodyById(asteroid.bodyId);
            if (!asteroidBody || !asteroidBody->active || asteroid.remainingMass <= 0.0) {
                continue;
            }
            double distance = std::hypot(body->position.x - asteroidBody->position.x, body->position.y - asteroidBody->position.y);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = &asteroid;
            }
        }
        if (!nearest || bestDistance > 50.0) {
            continue;
        }
        extractOre(world, ship, *nearest, dt);
    }
}

} // namespace orbital::gameplay
