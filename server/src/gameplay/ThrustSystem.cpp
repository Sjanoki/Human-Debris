#include "gameplay/ThrustSystem.hpp"

#include <algorithm>
#include <cmath>

#include "util/Vec2.hpp"

namespace orbital::gameplay {

using orbital::core::Body;
using orbital::core::Ship;
using orbital::core::ShipClass;
using orbital::core::World;
using orbital::util::Vec2;

void ThrustSystem::process(World& world) {
    auto commands = world.commandQueues.thrustCommands;
    world.commandQueues.thrustCommands.clear();

    for (const auto& cmd : commands) {
        Ship* ship = world.findShipById(cmd.shipId);
        if (!ship) {
            continue;
        }
        Body* body = world.findBodyById(ship->bodyId);
        if (!body || !body->active) {
            continue;
        }
        ShipClass* shipClass = world.findShipClass(ship->shipClassId);
        if (!shipClass) {
            continue;
        }
        double throttle = std::clamp(cmd.throttle, 0.0, 1.0);
        double duration = std::max(0.0, cmd.duration);
        double maxDuration = 5.0;
        duration = std::min(duration, maxDuration);
        double fuelUse = throttle * shipClass->engine.fuelUsePerSecondAtFullThrust * duration;
        if (fuelUse > ship->fuelMass) {
            duration *= (ship->fuelMass / fuelUse);
            fuelUse = ship->fuelMass;
        }
        if (fuelUse <= 0.0) {
            continue;
        }
        double force = throttle * shipClass->engine.maxThrust;
        double accel = force / std::max(1.0, body->mass);
        Vec2 forward{std::cos(body->angle), std::sin(body->angle)};
        body->velocity += forward * (accel * duration);
        ship->fuelMass -= fuelUse;
    }
}

} // namespace orbital::gameplay
