#include "core/BlueprintLoader.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "third_party/json.hpp"
#include "util/Logger.hpp"

namespace orbital::core {

using nlohmann::json;
using orbital::util::Logger;
namespace fs = std::filesystem;

namespace {
Vec2 cellCenter(int x, int y, int w, int h, double scale) {
    double cx = (static_cast<double>(x) - static_cast<double>(w) / 2.0 + 0.5) * scale;
    double cy = (static_cast<double>(h) / 2.0 - static_cast<double>(y) - 0.5) * scale;
    return {cx, cy};
}

Vec2 normalizeSafe(const Vec2& v) {
    double len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len <= 1e-6) {
        return {0.0, 1.0};
    }
    return {v.x / len, v.y / len};
}
} // namespace

BlueprintLoader::BlueprintLoader(std::string blueprintDir) : blueprintDir_(std::move(blueprintDir)) {}

bool BlueprintLoader::loadAll(BlueprintLibrary& library) {
    if (!fs::exists(blueprintDir_)) {
        ORBITAL_LOG(Logger::Level::Warning, "Blueprint directory missing: ", blueprintDir_);
        return false;
    }
    for (const auto& entry : fs::directory_iterator(blueprintDir_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream file(entry.path());
        if (!file) {
            continue;
        }
        json data;
        try {
            file >> data;
        } catch (const std::exception& e) {
            ORBITAL_LOG(Logger::Level::Error, "Failed to parse blueprint ", entry.path().string(), ": ", e.what());
            continue;
        }
        Blueprint bp;
        bp.id = data.value("id", entry.path().stem().string());
        bp.name = data.value("name", bp.id);
        auto grid = data.value("grid_size", json::object());
        bp.grid_width = grid.value("w", 0);
        bp.grid_height = grid.value("h", 0);
        bp.pixel_scale_m = data.value("pixel_scale_m", 1.0);

        auto layers = data.value("layers", json::object());
        auto hit = layers.value("hit", json::array());
        auto systems = layers.value("systems", json::array());
        if (hit.is_array()) {
            for (const auto& row : hit) {
                bp.hit_layer.rows.push_back(row.get<std::string>());
            }
        }
        if (systems.is_array()) {
            for (const auto& row : systems) {
                bp.systems_layer.rows.push_back(row.get<std::string>());
            }
        }
        bp.hit_layer.height = static_cast<int>(bp.hit_layer.rows.size());
        bp.hit_layer.width = bp.hit_layer.height > 0 ? static_cast<int>(bp.hit_layer.rows.front().size()) : 0;
        bp.systems_layer.height = static_cast<int>(bp.systems_layer.rows.size());
        bp.systems_layer.width = bp.systems_layer.height > 0 ? static_cast<int>(bp.systems_layer.rows.front().size()) : 0;

        // Derive hull/mass properties from hit layer.
        const double massPerCell = 200.0;
        Vec2 com{0.0, 0.0};
        double totalMass = 0.0;
        double inertia = 0.0;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (int y = 0; y < bp.hit_layer.height; ++y) {
            if (static_cast<int>(bp.hit_layer.rows[y].size()) < bp.hit_layer.width) {
                continue;
            }
            for (int x = 0; x < bp.hit_layer.width; ++x) {
                if (bp.hit_layer.rows[y][x] != 'X') {
                    continue;
                }
                Vec2 c = cellCenter(x, y, bp.hit_layer.width, bp.hit_layer.height, bp.pixel_scale_m);
                minX = std::min(minX, c.x);
                maxX = std::max(maxX, c.x);
                minY = std::min(minY, c.y);
                maxY = std::max(maxY, c.y);
                totalMass += massPerCell;
                com.x += c.x * massPerCell;
                com.y += c.y * massPerCell;
                inertia += massPerCell * (c.x * c.x + c.y * c.y);
            }
        }
        if (totalMass > 0.0) {
            com.x /= totalMass;
            com.y /= totalMass;
        }
        bp.mass = totalMass > 0.0 ? totalMass : 1.0;
        bp.center_of_mass = com;
        bp.moment_of_inertia = inertia > 0.0 ? inertia : 1.0;
        if (maxX > minX && maxY > minY) {
            bp.hull_polygon = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
        }

        // Engines from E/F pairs
        for (int y = 0; y < bp.systems_layer.height; ++y) {
            const auto& row = bp.systems_layer.rows[y];
            for (int x = 0; x < bp.systems_layer.width && x < static_cast<int>(row.size()); ++x) {
                if (row[x] != 'E') {
                    continue;
                }
                Vec2 engineCell = cellCenter(x, y, bp.systems_layer.width, bp.systems_layer.height, bp.pixel_scale_m);
                const std::vector<std::pair<int, int>> dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (auto [dx, dy] : dirs) {
                    int nx = x + dx;
                    int ny = y + dy;
                    if (ny < 0 || ny >= bp.systems_layer.height || nx < 0 || nx >= bp.systems_layer.width) {
                        continue;
                    }
                    if (bp.systems_layer.rows[ny][nx] != 'F') {
                        continue;
                    }
                    Vec2 flameCell = cellCenter(nx, ny, bp.systems_layer.width, bp.systems_layer.height, bp.pixel_scale_m);
                    Vec2 dir = normalizeSafe({flameCell.x - engineCell.x, flameCell.y - engineCell.y});
                    EngineDescriptor desc;
                    desc.local_pos = {(engineCell.x + flameCell.x) * 0.5, (engineCell.y + flameCell.y) * 0.5};
                    desc.local_dir = dir;
                    bp.engines.push_back(desc);
                    break;
                }
            }
        }

        // Docking ports
        for (int y = 0; y < bp.systems_layer.height; ++y) {
            const auto& row = bp.systems_layer.rows[y];
            for (int x = 0; x < bp.systems_layer.width && x < static_cast<int>(row.size()); ++x) {
                if (row[x] != 'd') {
                    continue;
                }
                Vec2 c = cellCenter(x, y, bp.systems_layer.width, bp.systems_layer.height, bp.pixel_scale_m);
                Vec2 dir = normalizeSafe(c);
                DockPortDescriptor port;
                port.local_pos = c;
                port.local_dir = dir;
                port.clearance = bp.pixel_scale_m * 0.75;
                bp.docking_ports.push_back(port);
            }
        }

        library.add(std::move(bp));
    }
    ORBITAL_LOG(Logger::Level::Info, "Loaded ", library.all().size(), " blueprints from ", blueprintDir_);
    return true;
}

} // namespace orbital::core

