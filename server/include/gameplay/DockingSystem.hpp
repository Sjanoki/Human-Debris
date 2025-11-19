#pragma once

#include "core/World.hpp"

namespace orbital::gameplay {

class DockingSystem {
public:
    void process(core::World& world);
    void processUndock(core::World& world);
};

} // namespace orbital::gameplay
