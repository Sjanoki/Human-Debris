#include "physics/PhysicsSystem.hpp"

#include <algorithm>

#include "util/Vec2.hpp"

namespace orbital::physics {

using orbital::core::Body;
using orbital::core::World;
using orbital::util::Vec2;
using orbital::util::length;

void PhysicsSystem::step(World& world, double dt) {
    applyGravity(world, dt);
    integrate(world, dt);
    handlePlanetCollision(world);
}

void PhysicsSystem::applyGravity(World& world, double dt) {
    if (!world.simulation.gravityEnabled) {
        return;
    }
    for (auto& body : world.bodies) {
        if (!body.active) {
            continue;
        }
        Vec2 r = body.position;
        double distSq = length(r);
        double rmag = length(r);
        if (rmag < 1.0) {
            continue;
        }
        double accelMag = -world.planet.mu / (rmag * rmag * rmag);
        Vec2 accel = accelMag * r;
        body.velocity += accel * dt;
    }
}

void PhysicsSystem::integrate(World& world, double dt) {
    for (auto& body : world.bodies) {
        if (!body.active) {
            continue;
        }
        body.position += body.velocity * dt;
        body.angle += body.angularVelocity * dt;
    }
}

void PhysicsSystem::handlePlanetCollision(World& world) {
    double radiusSq = world.planet.radius * world.planet.radius;
    for (auto& body : world.bodies) {
        if (!body.active) {
            continue;
        }
        double distSq = body.position.x * body.position.x + body.position.y * body.position.y;
        if (distSq < radiusSq) {
            body.active = false;
        }
    }
}

} // namespace orbital::physics
