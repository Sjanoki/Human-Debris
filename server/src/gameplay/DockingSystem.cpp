#include "gameplay/DockingSystem.hpp"

#include <algorithm>
#include <cmath>

#include "util/Vec2.hpp"

namespace orbital::gameplay {

using orbital::core::Body;
using orbital::core::DockCommand;
using orbital::core::DockingPort;
using orbital::core::Player;
using orbital::core::Station;
using orbital::core::World;
using orbital::util::Vec2;
using orbital::util::length;

namespace {
Vec2 rotateVec(const Vec2& v, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
}

void DockingSystem::process(World& world) {
    auto commands = world.commandQueues.dockCommands;
    world.commandQueues.dockCommands.clear();
    for (const auto& cmd : commands) {
        auto* ship = world.findShipById(cmd.shipId);
        auto* station = world.findStationById(cmd.stationId);
        if (!ship || !station) {
            continue;
        }
        auto* shipBody = world.findBodyById(ship->bodyId);
        auto* stationBody = world.findBodyById(station->bodyId);
        if (!shipBody || !stationBody) {
            continue;
        }
        bool docked = false;
        for (const auto& port : station->dockingPorts) {
            Vec2 portWorldPos = stationBody->position + rotateVec(port.localPosition, stationBody->angle);
            Vec2 portForward = rotateVec(port.localForward, stationBody->angle);
            Vec2 delta = shipBody->position - portWorldPos;
            double distance = length(delta);
            if (distance > port.radius) {
                continue;
            }
            double approachSpeed = length(shipBody->velocity - stationBody->velocity);
            if (approachSpeed > port.maxApproachSpeed) {
                continue;
            }
            Vec2 shipForward{std::cos(shipBody->angle), std::sin(shipBody->angle)};
            double dotForward = shipForward.x * portForward.x + shipForward.y * portForward.y;
            double angleDiff = std::acos(std::clamp(dotForward, -1.0, 1.0));
            if (angleDiff > port.maxAngleDiff) {
                continue;
            }
            shipBody->position = portWorldPos;
            shipBody->velocity = stationBody->velocity;
            shipBody->angle = std::atan2(portForward.y, portForward.x);
            shipBody->angularVelocity = 0.0;
            ship->docked = true;
            ship->dockedStationId = station->id;
            docked = true;
            break;
        }
        if (!docked) {
            continue;
        }
    }
}

void DockingSystem::processUndock(World& world) {
    auto commands = world.commandQueues.undockCommands;
    world.commandQueues.undockCommands.clear();
    for (const auto& cmd : commands) {
        auto* ship = world.findShipById(cmd.shipId);
        if (!ship || !ship->docked) {
            continue;
        }
        auto* body = world.findBodyById(ship->bodyId);
        if (!body) {
            continue;
        }
        ship->docked = false;
        ship->dockedStationId = -1;
        body->velocity += Vec2{0.0, 0.5};
    }
}

} // namespace orbital::gameplay
