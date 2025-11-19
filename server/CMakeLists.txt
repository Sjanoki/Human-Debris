cmake_minimum_required(VERSION 3.16)

set(SERVER_SOURCES
    src/main.cpp
    src/util/Logger.cpp
    src/util/Vec2.cpp
    src/core/World.cpp
    src/core/ConfigLoader.cpp
    src/core/WorldBuilder.cpp
    src/physics/PhysicsSystem.cpp
    src/gameplay/ThrustSystem.cpp
    src/gameplay/MiningSystem.cpp
    src/gameplay/DockingSystem.cpp
    src/gameplay/EconomySystem.cpp
    src/gameplay/RadarSystem.cpp
    src/gameplay/PlayerSessionSystem.cpp
    src/net/TcpServer.cpp
)

add_executable(orbital_server ${SERVER_SOURCES})

target_include_directories(orbital_server PRIVATE include)

target_link_libraries(orbital_server PRIVATE pthread)
