#pragma once

#include <string>
#include <vector>

#include "core/World.hpp"
#include "third_party/json.hpp"

namespace orbital::core {

class ConfigLoader {
public:
    explicit ConfigLoader(std::string configDir);

    bool loadPlanet(Planet& planet);
    bool loadSimulation(SimulationConfig& simulation);
    bool loadShipClasses(std::vector<ShipClass>& shipClasses);
    bool loadStations(std::vector<Station>& stations, std::vector<Body>& bodies, std::vector<PolygonCollider>& colliders,
                     int& nextBodyId);
    bool loadAsteroids(std::vector<Asteroid>& asteroids, std::vector<Body>& bodies, std::vector<PolygonCollider>& colliders, int& nextBodyId);

private:
    std::string configDir_;

    bool loadJsonFile(const std::string& path, nlohmann::json& outJson);
};

} // namespace orbital::core
