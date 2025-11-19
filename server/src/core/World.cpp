#include "core/World.hpp"

namespace orbital::core {

PolygonCollider* World::findCollider(const std::string& id) {
    for (auto& collider : colliders) {
        if (collider.id == id) {
            return &collider;
        }
    }
    return nullptr;
}

ShipClass* World::findShipClass(const std::string& id) {
    for (auto& cls : shipClasses) {
        if (cls.id == id) {
            return &cls;
        }
    }
    return nullptr;
}

Ship* World::findShipById(int id) {
    for (auto& ship : ships) {
        if (ship.id == id) {
            return &ship;
        }
    }
    return nullptr;
}

Body* World::findBodyById(int id) {
    for (auto& body : bodies) {
        if (body.id == id) {
            return &body;
        }
    }
    return nullptr;
}

Player* World::findPlayerById(int id) {
    for (auto& player : players) {
        if (player.id == id) {
            return &player;
        }
    }
    return nullptr;
}

Player* World::findPlayerByConnection(int connectionId) {
    for (auto& player : players) {
        if (player.connectionId == connectionId) {
            return &player;
        }
    }
    return nullptr;
}

Station* World::findStationById(int id) {
    for (auto& station : stations) {
        if (station.id == id) {
            return &station;
        }
    }
    return nullptr;
}

Asteroid* World::findAsteroidByBody(int bodyId) {
    for (auto& asteroid : asteroids) {
        if (asteroid.bodyId == bodyId) {
            return &asteroid;
        }
    }
    return nullptr;
}

CargoHold* World::findCargoHoldById(int id) {
    for (auto& hold : cargoHolds) {
        if (hold.id == id) {
            return &hold;
        }
    }
    return nullptr;
}

} // namespace orbital::core
