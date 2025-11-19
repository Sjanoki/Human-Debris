#pragma once

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/Vec2.hpp"

namespace orbital::core {

using orbital::util::Vec2;

struct Planet {
    double mu{3.986004418e14};
    double radius{6.371e6};
};

enum class BodyType { Ship, Asteroid, Station };

struct PolygonCollider {
    std::string id;
    std::vector<Vec2> verticesLocal;
    double boundingRadius{0.0};
    Vec2 centroidLocal{0.0, 0.0};
};

struct OrbitDescriptor {
    double semiMajorAxis{0.0};
    double eccentricity{0.0};
    double period{0.0};
};

struct Body {
    int id{-1};
    BodyType type{BodyType::Ship};
    Vec2 position;
    Vec2 velocity;
    double angle{0.0};
    double angularVelocity{0.0};
    double mass{1.0};
    double inertia{1.0};
    int colliderId{-1};
    bool active{true};
    OrbitDescriptor orbit;
};

struct EngineType {
    std::string name;
    double maxThrust{0.0};
    double fuelUsePerSecondAtFullThrust{0.0};
};

struct ShipClass {
    std::string id;
    double baseMass{0.0};
    double cargoCapacity{0.0};
    double radarRange{0.0};
    double maxRotationRate{0.0};
    EngineType engine;
    std::string colliderShapeId;
};

struct ShipControlState {
    bool thrust{false};
    bool turnLeft{false};
    bool turnRight{false};
    bool mine{false};
};

struct Ship {
    int id{-1};
    int bodyId{-1};
    std::string shipClassId;
    double fuelMass{0.0};
    double maxFuelMass{0.0};
    int cargoHoldId{-1};
    int ownerPlayerId{-1};
    bool docked{false};
    int dockedStationId{-1};
    ShipControlState controlState;
};

struct Asteroid {
    int id{-1};
    int bodyId{-1};
    double remainingMass{0.0};
    double purity{0.0};
    double density{0.0};
    double initialMass{0.0};
};

struct DockingPort {
    Vec2 localPosition;
    Vec2 localForward{0.0, 1.0};
    double radius{5.0};
    double maxApproachSpeed{1.0};
    double maxAngleDiff{0.2};
};

struct ShipOffer {
    std::string shipClassId;
    double price{0.0};
};

struct StationMarket {
    double basePricePerKg{10.0};
    double purityMultiplier{20.0};
    std::vector<ShipOffer> shipOffers;
};

struct Station {
    int id{-1};
    int bodyId{-1};
    std::vector<DockingPort> dockingPorts;
    StationMarket market;
};

enum class CargoType { Ore };

struct CargoItem {
    CargoType type{CargoType::Ore};
    double mass{0.0};
    double purity{0.0};
};

struct CargoHold {
    int id{-1};
    double capacityMass{0.0};
    double currentMass{0.0};
    std::vector<CargoItem> items;
};

struct Player {
    int id{-1};
    std::string name;
    double credits{0.0};
    std::vector<int> ownedShipIds;
    int activeShipId{-1};
    bool online{false};
    int dockedStationId{-1};
    int connectionId{-1};
    bool worldStateSubscribed{false};
};

struct SimulationConfig {
    double timeStep{0.1};
    bool gravityEnabled{true};
    double defaultPlayerCredits{1000.0};
    std::string defaultShipClassId{"SCOUT"};
    int defaultSpawnStationId{1};
};

struct ThrustCommand {
    int shipId{-1};
    double throttle{0.0};
    double duration{0.0};
};

struct RotateCommand {
    int shipId{-1};
    double direction{0.0};
    double duration{0.0};
};

struct MineCommand {
    int shipId{-1};
    int asteroidId{-1};
    double duration{0.0};
};

struct DockCommand {
    int shipId{-1};
    int stationId{-1};
};

struct SellOreCommand {
    int shipId{-1};
    int stationId{-1};
};

struct BuyShipCommand {
    int stationId{-1};
    std::string shipClassId;
};

struct SwitchShipCommand {
    int newShipId{-1};
};

struct CommandQueues {
    std::vector<ThrustCommand> thrustCommands;
    std::vector<RotateCommand> rotateCommands;
    std::vector<MineCommand> mineCommands;
    std::vector<DockCommand> dockCommands;
    std::vector<DockCommand> undockCommands;
    std::vector<SellOreCommand> sellCommands;
    std::vector<BuyShipCommand> buyCommands;
    std::vector<SwitchShipCommand> switchCommands;
};

struct World {
    Planet planet;
    SimulationConfig simulation;
    std::vector<PolygonCollider> colliders;
    std::vector<Body> bodies;
    std::vector<Ship> ships;
    std::vector<Asteroid> asteroids;
    std::vector<Station> stations;
    std::vector<CargoHold> cargoHolds;
    std::vector<Player> players;
    std::vector<ShipClass> shipClasses;
    CommandQueues commandQueues;

    int nextBodyId{1};
    int nextShipId{1};
    int nextCargoHoldId{1};
    int nextPlayerId{1};

    PolygonCollider* findCollider(const std::string& id);
    ShipClass* findShipClass(const std::string& id);
    Ship* findShipById(int id);
    Body* findBodyById(int id);
    Player* findPlayerById(int id);
    Player* findPlayerByConnection(int connectionId);
    Station* findStationById(int id);
    Asteroid* findAsteroidByBody(int bodyId);
    CargoHold* findCargoHoldById(int id);
};

} // namespace orbital::core
