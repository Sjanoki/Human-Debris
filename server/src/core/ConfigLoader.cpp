#include "core/ConfigLoader.hpp"

#include <fstream>

#include "third_party/json.hpp"
#include "util/Logger.hpp"

namespace orbital::core {

using nlohmann::json;
using orbital::util::Logger;

namespace {
orbital::core::PolygonCollider* findColliderLocal(std::vector<orbital::core::PolygonCollider>& colliders,
                                                  const std::string& id) {
    for (auto& collider : colliders) {
        if (collider.id == id) {
            return &collider;
        }
    }
    return nullptr;
}
} // namespace

ConfigLoader::ConfigLoader(std::string configDir) : configDir_(std::move(configDir)) {}

bool ConfigLoader::loadJsonFile(const std::string& path, json& outJson) {
    std::ifstream file(path);
    if (!file) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to open config file: ", path);
        return false;
    }
    try {
        file >> outJson;
    } catch (const std::exception& e) {
        ORBITAL_LOG(Logger::Level::Error, "JSON parse error in ", path, ": ", e.what());
        return false;
    }
    return true;
}

bool ConfigLoader::loadPlanet(Planet& planet) {
    json data;
    if (!loadJsonFile(configDir_ + "/planet.json", data)) {
        return false;
    }
    planet.mu = data.value("mu", planet.mu);
    planet.radius = data.value("radius", planet.radius);
    return true;
}

bool ConfigLoader::loadSimulation(SimulationConfig& simulation) {
    json data;
    if (!loadJsonFile(configDir_ + "/simulation.json", data)) {
        return false;
    }
    simulation.timeStep = data.value("timeStep", simulation.timeStep);
    simulation.gravityEnabled = data.value("gravityEnabled", simulation.gravityEnabled);
    simulation.defaultPlayerCredits = data.value("defaultPlayerCredits", simulation.defaultPlayerCredits);
    simulation.defaultShipClassId = data.value("defaultShipClassId", simulation.defaultShipClassId);
    simulation.defaultSpawnStationId = data.value("defaultSpawnStationId", simulation.defaultSpawnStationId);
    return true;
}

bool ConfigLoader::loadShipClasses(std::vector<ShipClass>& shipClasses) {
    json data;
    if (!loadJsonFile(configDir_ + "/ship_classes.json", data)) {
        return false;
    }
    if (!data.is_array()) {
        return false;
    }
    for (const auto& entry : data) {
        ShipClass cls;
        cls.id = entry.value("id", "SCOUT");
        cls.baseMass = entry.value("baseMass", 1.0);
        cls.cargoCapacity = entry.value("cargoCapacity", 0.0);
        cls.radarRange = entry.value("radarRange", 1000.0);
        cls.maxRotationRate = entry.value("maxRotationRate", 0.2);
        cls.maxFuelMass = entry.value("maxFuel", 0.0);
        cls.colliderShapeId = entry.value("colliderShapeId", "ship_triangle");
        cls.engineType = entry.value("engineType", "");
        cls.weaponType = entry.value("weaponType", "none");
        auto vertices = entry.value("shape_vertices", json::array());
        if (vertices.is_array()) {
            for (const auto& v : vertices) {
                auto arr = v.get<std::vector<double>>();
                if (arr.size() == 2) {
                    cls.shapeVertices.emplace_back(arr[0], arr[1]);
                }
            }
        }
        cls.shapeScaleMeters = entry.value("shape_scale_m", 0.0);
        auto engineJson = entry.value("engine", json::object());
        cls.engine.name = engineJson.value("name", "Engine");
        cls.engine.maxThrust = engineJson.value("maxThrust", 10.0);
        cls.engine.fuelUsePerSecondAtFullThrust = engineJson.value("fuelUsePerSecondAtFullThrust", 1.0);
        shipClasses.push_back(cls);
    }
    return true;
}

bool ConfigLoader::loadStations(std::vector<Station>& stations, std::vector<Body>& bodies,
                                std::vector<PolygonCollider>& colliders, int& nextBodyId) {
    json data;
    if (!loadJsonFile(configDir_ + "/stations.json", data)) {
        return false;
    }
    if (!data.is_array()) {
        return false;
    }
    for (const auto& entry : data) {
        Station station;
        station.id = entry.value("id", 1);
        Body body;
        body.id = nextBodyId++;
        body.type = BodyType::Station;
        auto position = entry.value("initialPosition", std::vector<double>{0.0, 0.0});
        if (position.size() == 2) {
            body.position = {position[0], position[1]};
        }
        auto velocity = entry.value("initialVelocity", std::vector<double>{0.0, 0.0});
        if (velocity.size() == 2) {
            body.velocity = {velocity[0], velocity[1]};
        }
        auto vertices = entry.value("shape_vertices", json::array());
        if (vertices.is_array()) {
            for (const auto& v : vertices) {
                auto arr = v.get<std::vector<double>>();
                if (arr.size() == 2) {
                    station.shapeVertices.emplace_back(arr[0], arr[1]);
                }
            }
        }
        station.shapeScaleMeters = entry.value("shape_scale_m", station.shapeScaleMeters);
        std::string colliderId = entry.value("colliderShapeId", "station_circle");
        auto collider = findColliderLocal(colliders, colliderId);
        if (!collider) {
            PolygonCollider poly;
            poly.id = colliderId;
            poly.verticesLocal = {{-20, -20}, {20, -20}, {20, 20}, {-20, 20}};
            poly.boundingRadius = 28.0;
            colliders.push_back(poly);
        }
        body.colliderId = static_cast<int>(colliders.size() - 1);
        body.mass = 1e6;
        station.bodyId = body.id;
        body.angle = 0.0;
        body.angularVelocity = 0.0;
        bodies.push_back(body);

        auto ports = entry.value("dockingPorts", json::array());
        for (const auto& portJson : ports) {
            DockingPort port;
            auto pos = portJson.value("localPosition", std::vector<double>{0.0, 0.0});
            if (pos.size() == 2) {
                port.localPosition = {pos[0], pos[1]};
            }
            auto fwd = portJson.value("localForward", std::vector<double>{0.0, 1.0});
            if (fwd.size() == 2) {
                port.localForward = {fwd[0], fwd[1]};
            }
            port.radius = portJson.value("radius", 10.0);
            port.maxApproachSpeed = portJson.value("maxApproachSpeed", 1.0);
            port.maxAngleDiff = portJson.value("maxAngleDiff", 0.5);
            station.dockingPorts.push_back(port);
        }

        auto marketJson = entry.value("market", json::object());
        station.market.basePricePerKg = marketJson.value("basePricePerKg", 10.0);
        station.market.purityMultiplier = marketJson.value("purityMultiplier", 20.0);
        auto offers = marketJson.value("shipOffers", json::array());
        for (const auto& offer : offers) {
            ShipOffer shipOffer;
            shipOffer.shipClassId = offer.value("shipClassId", "SCOUT");
            shipOffer.price = offer.value("price", 1000.0);
            station.market.shipOffers.push_back(shipOffer);
        }

        stations.push_back(station);
    }
    return true;
}

bool ConfigLoader::loadAsteroids(std::vector<Asteroid>& asteroids, std::vector<Body>& bodies, std::vector<PolygonCollider>& colliders, int& nextBodyId) {
    json data;
    if (!loadJsonFile(configDir_ + "/asteroid_fields.json", data)) {
        return false;
    }
    if (!data.is_array()) {
        return false;
    }
    for (const auto& entry : data) {
        Body body;
        body.id = nextBodyId++;
        body.type = BodyType::Asteroid;
        auto position = entry.value("position", std::vector<double>{0.0, 0.0});
        if (position.size() == 2) {
            body.position = {position[0], position[1]};
        }
        auto velocity = entry.value("velocity", std::vector<double>{0.0, 0.0});
        if (velocity.size() == 2) {
            body.velocity = {velocity[0], velocity[1]};
        }
        double mass = entry.value("remainingMass", 1000.0);
        body.mass = mass;
        body.inertia = 0.5 * mass * 25.0;
        std::string colliderId = entry.value("colliderShapeId", "asteroid_circle");
        auto collider = findColliderLocal(colliders, colliderId);
        if (!collider) {
            PolygonCollider poly;
            poly.id = colliderId;
            poly.verticesLocal = {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}};
            poly.boundingRadius = 7.5;
            colliders.push_back(poly);
        }
        body.colliderId = static_cast<int>(colliders.size() - 1);
        bodies.push_back(body);

        Asteroid asteroid;
        asteroid.id = static_cast<int>(asteroids.size() + 1);
        asteroid.bodyId = body.id;
        asteroid.remainingMass = mass;
        asteroid.initialMass = mass;
        asteroid.density = entry.value("density", 2500.0);
        asteroid.purity = entry.value("purity", 0.5);
        asteroids.push_back(asteroid);
    }
    return true;
}

} // namespace orbital::core
