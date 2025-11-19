#pragma once

#include <string>

#include "core/ConfigLoader.hpp"

namespace orbital::core {

class WorldBuilder {
public:
    explicit WorldBuilder(std::string configDir);

    World build();

private:
    std::string configDir_;
};

} // namespace orbital::core
