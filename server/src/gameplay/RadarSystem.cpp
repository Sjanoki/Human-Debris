#include "gameplay/RadarSystem.hpp"

#include <cmath>

#include "util/Vec2.hpp"

namespace orbital::gameplay {

using orbital::core::Body;
using orbital::core::Ship;
using orbital::core::ShipClass;
using orbital::core::World;
using orbital::util::Vec2;

std::vector<RadarContact> RadarSystem::scan(World& world, int shipId) {
    std::vector<RadarContact> contacts;
    Ship* ship = world.findShipById(shipId);
    if (!ship) {
        return contacts;
    }
    Body* body = world.findBodyById(ship->bodyId);
    if (!body) {
        return contacts;
    }
    ShipClass* shipClass = world.findShipClass(ship->shipClassId);
    if (!shipClass) {
        return contacts;
    }
    for (auto& other : world.bodies) {
        if (!other.active || other.id == body->id) {
            continue;
        }
        Vec2 delta = other.position - body->position;
        double distance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (distance > shipClass->radarRange) {
            continue;
        }
        RadarContact contact;
        contact.bodyId = other.id;
        contact.type = other.type;
        contact.distance = distance;
        contact.bearing = std::atan2(delta.y, delta.x);
        contacts.push_back(contact);
    }
    return contacts;
}

} // namespace orbital::gameplay
