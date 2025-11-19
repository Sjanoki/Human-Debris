#include "gameplay/PlayerSessionSystem.hpp"

#include "util/Logger.hpp"

namespace orbital::gameplay {

using orbital::core::Player;
using orbital::core::World;
using orbital::util::Logger;

Player* PlayerSessionSystem::login(World& world, const std::string& name, int connectionId) {
    for (auto& player : world.players) {
        if (player.name == name) {
            player.online = true;
            player.connectionId = connectionId;
            player.worldStateSubscribed = false;
            ORBITAL_LOG(Logger::Level::Info, "Player ", name, " logged in");
            return &player;
        }
    }
    Player player;
    player.id = world.nextPlayerId++;
    player.name = name;
    player.credits = world.simulation.defaultPlayerCredits;
    player.online = true;
    player.connectionId = connectionId;
    player.worldStateSubscribed = false;
    world.players.push_back(player);
    ORBITAL_LOG(Logger::Level::Info, "Created new player ", name);
    return &world.players.back();
}

void PlayerSessionSystem::logout(World& world, int connectionId) {
    for (auto& player : world.players) {
        if (player.connectionId == connectionId) {
            player.online = false;
            player.connectionId = -1;
            player.worldStateSubscribed = false;
            ORBITAL_LOG(Logger::Level::Info, "Player ", player.name, " logged out");
            break;
        }
    }
}

} // namespace orbital::gameplay
