#include "gameplay/EconomySystem.hpp"

#include <algorithm>

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
                    orbital::core::Body body;
                    body.id = world.nextBodyId++;
                    body.type = core::BodyType::Ship;
                    body.mass = cls->baseMass;
                    body.colliderId = 0;
                    world.bodies.push_back(body);

                    CargoHold hold;
                    hold.id = world.nextCargoHoldId++;
                    hold.capacityMass = cls->cargoCapacity;
                    world.cargoHolds.push_back(hold);

                    Ship ship;
                    ship.id = world.nextShipId++;
                    ship.shipClassId = cls->id;
                    ship.bodyId = body.id;
                    ship.fuelMass = 100.0;
                    ship.maxFuelMass = 100.0;
                    ship.cargoHoldId = hold.id;
                    ship.ownerPlayerId = player.id;
                    world.ships.push_back(ship);
                    player.ownedShipIds.push_back(ship.id);
                    break;
                }
                break;
            }
        }
    }
}

} // namespace orbital::gameplay
