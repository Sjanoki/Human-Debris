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

namespace {
Vec2 rotateVec(const Vec2& v, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
}

void ThrustSystem::process(World& world) {
    double dt = world.simulation.timeStep;
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
        Vec2 totalForce{0.0, 0.0};
        const auto* bp = world.findBlueprint(ship->blueprintId);
        if (bp && !bp->engines.empty()) {
            double perEngineForce = force / static_cast<double>(bp->engines.size());
            for (const auto& engine : bp->engines) {
                Vec2 worldDir = rotateVec(engine.local_dir, body->angle);
                totalForce += worldDir * perEngineForce;
            }
        } else {
            totalForce = Vec2{-std::sin(body->angle), std::cos(body->angle)} * force;
        }
        Vec2 accelVec = totalForce / std::max(1.0, body->mass);
        body->velocity += accelVec * duration;
        ship->fuelMass -= fuelUse;
    }

    for (auto& ship : world.ships) {
        if (ship.docked) {
            continue;
        }
        Body* body = world.findBodyById(ship.bodyId);
        if (!body || !body->active) {
            continue;
        }
        ShipClass* shipClass = world.findShipClass(ship.shipClassId);
        if (!shipClass) {
            continue;
        }
        double turnInput = 0.0;
        if (ship.controlState.turnLeft) {
            turnInput -= 1.0;
        }
        if (ship.controlState.turnRight) {
            turnInput += 1.0;
        }
        if (turnInput != 0.0) {
            body->angle += shipClass->maxRotationRate * dt * turnInput;
        }

        if (!ship.controlState.thrust || ship.fuelMass <= 0.0) {
            continue;
        }
        double desiredFuel = shipClass->engine.fuelUsePerSecondAtFullThrust * dt;
        if (desiredFuel <= 0.0) {
            continue;
        }
        double fuelUsed = std::min(desiredFuel, ship.fuelMass);
        double effectiveDt = dt;
        if (fuelUsed < desiredFuel) {
            effectiveDt *= (fuelUsed / desiredFuel);
        }
        if (effectiveDt <= 0.0) {
            continue;
        }
        double force = shipClass->engine.maxThrust;
        Vec2 totalForce{0.0, 0.0};
        const auto* bp = world.findBlueprint(ship->blueprintId);
        if (bp && !bp->engines.empty()) {
            double perEngineForce = force / static_cast<double>(bp->engines.size());
            for (const auto& engine : bp->engines) {
                Vec2 worldDir = rotateVec(engine.local_dir, body->angle);
                totalForce += worldDir * perEngineForce;
            }
        } else {
            totalForce = Vec2{-std::sin(body->angle), std::cos(body->angle)} * force;
        }
        Vec2 accelVec = totalForce / std::max(1.0, body->mass);
        body->velocity += accelVec * effectiveDt;
        ship.fuelMass -= fuelUsed;
    }
}

} // namespace orbital::gameplay
