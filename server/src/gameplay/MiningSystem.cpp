#include "gameplay/MiningSystem.hpp"

#include <algorithm>
#include <cmath>

namespace orbital::gameplay {

using orbital::core::Asteroid;
using orbital::core::Body;
using orbital::core::CargoHold;
using orbital::core::CargoItem;
using orbital::core::World;

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
        auto* body = world.findBodyById(ship->bodyId);
        auto* asteroidBody = world.findBodyById(asteroid->bodyId);
        if (!body || !asteroidBody) {
            continue;
        }
        double distance = std::sqrt((body->position.x - asteroidBody->position.x) * (body->position.x - asteroidBody->position.x) +
                                    (body->position.y - asteroidBody->position.y) * (body->position.y - asteroidBody->position.y));
        if (distance > 50.0) {
            continue;
        }
        CargoHold* hold = world.findCargoHoldById(ship->cargoHoldId);
        if (!hold) {
            continue;
        }
        double remainingCapacity = hold->capacityMass - hold->currentMass;
        if (remainingCapacity <= 0.0) {
            continue;
        }
        double rate = 5.0; // kg per second
        double extract = std::min({cmd.duration * rate, asteroid->remainingMass, remainingCapacity});
        if (extract <= 0.0) {
            continue;
        }
        asteroid->remainingMass -= extract;
        asteroidBody->mass -= extract;
        hold->currentMass += extract;
        bool foundOre = false;
        for (auto& item : hold->items) {
            if (item.type == core::CargoType::Ore && std::abs(item.purity - asteroid->purity) < 0.01) {
                item.mass += extract;
                foundOre = true;
                break;
            }
        }
        if (!foundOre) {
            CargoItem item;
            item.type = core::CargoType::Ore;
            item.mass = extract;
            item.purity = asteroid->purity;
            hold->items.push_back(item);
        }
        if (asteroid->remainingMass <= 1.0) {
            asteroidBody->active = false;
        }
    }
}

} // namespace orbital::gameplay
