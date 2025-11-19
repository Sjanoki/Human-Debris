#pragma once

#include "core/World.hpp"

namespace orbital::gameplay {

class EconomySystem {
public:
    void processSell(core::World& world);
    void processBuy(core::World& world);
};

} // namespace orbital::gameplay
