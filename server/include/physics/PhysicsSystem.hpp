#pragma once

#include "core/World.hpp"

namespace orbital::physics {

class PhysicsSystem {
public:
    void step(core::World& world, double dt);

private:
    void applyGravity(core::World& world, double dt);
    void integrate(core::World& world, double dt);
    void handlePlanetCollision(core::World& world);
};

} // namespace orbital::physics
