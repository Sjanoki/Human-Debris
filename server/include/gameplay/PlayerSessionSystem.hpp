#pragma once

#include <string>

#include "core/World.hpp"

namespace orbital::gameplay {

class PlayerSessionSystem {
public:
    core::Player* login(core::World& world, const std::string& name, int connectionId);
    void logout(core::World& world, int connectionId);
};

} // namespace orbital::gameplay
