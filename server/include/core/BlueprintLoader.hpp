#pragma once

#include <string>

#include "core/Blueprint.hpp"

namespace orbital::core {

class BlueprintLoader {
public:
    explicit BlueprintLoader(std::string blueprintDir);
    bool loadAll(BlueprintLibrary& library);

private:
    std::string blueprintDir_;
};

} // namespace orbital::core

