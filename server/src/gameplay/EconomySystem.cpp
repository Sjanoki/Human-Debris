#include "gameplay/EconomySystem.hpp"

#include <algorithm>
#include <cmath>

#include "util/Logger.hpp"

namespace orbital::gameplay {

using orbital::core::CargoHold;
using orbital::core::CargoItem;
using orbital::core::Player;
using orbital::core::Ship;
using orbital::core::ShipClass;
using orbital::core::Station;
using orbital::core::World;
using orbital::util::Logger;
using orbital::util::Vec2;

namespace {
Vec2 rotateVec(const Vec2& v, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
}

void EconomySystem::processSell(World& world) {
    auto commands = world.commandQueues.sellCommands;
    world.commandQueues.sellCommands.clear();
    for (const auto& cmd : commands) {
        Ship* ship = world.findShipById(cmd.shipId);
        Station* station = world.findStationById(cmd.stationId);
        if (!ship || !station) {
            continue;
        }
        if (!ship->docked || ship->dockedStationId != station->id) {
            continue;
        }
        Player* player = world.findPlayerById(ship->ownerPlayerId);
        if (!player) {
            continue;
        }
        CargoHold* hold = world.findCargoHoldById(ship->cargoHoldId);
        if (!hold) {
            continue;
        }
        double total = 0.0;
        for (auto& item : hold->items) {
            if (item.type != core::CargoType::Ore) {
                continue;
            }
            double pricePerKg = station->market.basePricePerKg + station->market.purityMultiplier * item.purity;
            total += pricePerKg * item.mass;
        }
        hold->items.clear();
        hold->currentMass = 0.0;
        player->credits += total;
        ORBITAL_LOG(Logger::Level::Info, "Player ", player->name, " sold ore for ", total, " credits");
    }
}

void EconomySystem::processBuy(World& world) {
    auto commands = world.commandQueues.buyCommands;
    world.commandQueues.buyCommands.clear();
    for (const auto& cmd : commands) {
        Station* station = world.findStationById(cmd.stationId);
        if (!station) {
            continue;
        }
        for (const auto& offer : station->market.shipOffers) {
            if (offer.shipClassId == cmd.shipClassId) {
                ShipClass* cls = world.findShipClass(offer.shipClassId);
                if (!cls) {
                    continue;
                }
                // Find a player docked at this station with enough credits.
                for (auto& player : world.players) {
                    if (player.dockedStationId != station->id) {
                        continue;
                    }
                    if (player.credits < offer.price) {
                        continue;
                    }
                    player.credits -= offer.price;
                    const auto* bp = world.findBlueprint(cls->blueprintId);
                    auto* stationBody = world.findBodyById(station->bodyId);
                    Vec2 dockPos = stationBody ? stationBody->position : Vec2{0.0, 0.0};
                    double dockAngle = stationBody ? stationBody->angle : 0.0;
                    if (stationBody && !station->dockingPorts.empty()) {
                        const auto& port = station->dockingPorts.front();
                        dockPos = stationBody->position + rotateVec(port.localPosition, stationBody->angle);
                        Vec2 fwd = rotateVec(port.localForward, stationBody->angle);
                        dockAngle = std::atan2(fwd.y, fwd.x);
                    }

                    orbital::core::Body body;
                    body.id = world.nextBodyId++;
                    body.type = core::BodyType::Ship;
                    body.mass = bp && bp->mass > 0.0 ? bp->mass : cls->baseMass;
                    body.inertia = bp && bp->moment_of_inertia > 0.0 ? bp->moment_of_inertia : body.mass;
                    body.colliderId = 0;
                    body.position = dockPos;
                    body.velocity = stationBody ? stationBody->velocity : Vec2{0.0, 0.0};
                    body.angle = dockAngle;
                    world.bodies.push_back(body);

                    CargoHold hold;
                    hold.id = world.nextCargoHoldId++;
                    hold.capacityMass = cls->cargoCapacity;
                    hold.currentMass = 0.0;
                    world.cargoHolds.push_back(hold);

                    Ship ship;
                    ship.id = world.nextShipId++;
                    ship.shipClassId = cls->id;
                    ship.blueprintId = cls->blueprintId;
                    ship.bodyId = body.id;
                    double fuelCap = cls->maxFuelMass > 0.0 ? cls->maxFuelMass : 100.0;
                    ship.fuelMass = fuelCap;
                    ship.maxFuelMass = fuelCap;
                    ship.cargoHoldId = hold.id;
                    ship.ownerPlayerId = player.id;
                    ship.docked = true;
                    ship.dockedStationId = station->id;
                    ship.atStationId = station->id;
                    world.ships.push_back(ship);
                    player.ownedShipIds.push_back(ship.id);
                    if (player.activeShipId == -1) {
                        player.activeShipId = ship.id;
                    }
                    break;
                }
                break;
            }
        }
    }
}

} // namespace orbital::gameplay
