#include "core/WorldBuilder.hpp"

#include <filesystem>

#include "core/BlueprintLoader.hpp"
#include "util/Logger.hpp"

namespace orbital::core {

using orbital::util::Logger;

WorldBuilder::WorldBuilder(std::string configDir) : configDir_(std::move(configDir)) {}

World WorldBuilder::build() {
    World world;
    ConfigLoader loader(configDir_);
    BlueprintLoader blueprintLoader(configDir_ + "/blueprints");
    blueprintLoader.loadAll(world.blueprints);
    loader.setBlueprintLibrary(&world.blueprints);
    loader.loadPlanet(world.planet);
    loader.loadSimulation(world.simulation);
    loader.loadShipClasses(world.shipClasses);

    // Default collider set for ships if config is missing.
    PolygonCollider triangle;
    triangle.id = "ship_triangle";
    triangle.verticesLocal = {{0, 3}, {-1.5, -1.5}, {1.5, -1.5}};
    triangle.boundingRadius = 3.0;
    world.colliders.push_back(triangle);

    loader.loadStations(world.stations, world.bodies, world.colliders, world.nextBodyId);

    loader.loadAsteroids(world.asteroids, world.bodies, world.colliders, world.nextBodyId);

    ORBITAL_LOG(Logger::Level::Info, "World initialized with ", world.bodies.size(), " bodies and ", world.shipClasses.size(),
                " ship classes");

    return world;
}

} // namespace orbital::core
