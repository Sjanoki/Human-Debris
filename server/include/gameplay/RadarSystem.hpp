#pragma once

#include <vector>

#include "core/World.hpp"

namespace orbital::gameplay {

struct RadarContact {
    int bodyId{-1};
    core::BodyType type{core::BodyType::Ship};
    double distance{0.0};
    double bearing{0.0};
};

class RadarSystem {
public:
    std::vector<RadarContact> scan(core::World& world, int shipId);
};

} // namespace orbital::gameplay
