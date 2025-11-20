#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "util/Vec2.hpp"

namespace orbital::core {

using orbital::util::Vec2;

struct EngineDescriptor {
    Vec2 local_pos;
    Vec2 local_dir{0.0, 1.0};
    double max_thrust{0.0};
};

struct DockPortDescriptor {
    Vec2 local_pos;
    Vec2 local_dir{0.0, 1.0};
    double clearance{5.0};
};

struct BlueprintLayer {
    int width{0};
    int height{0};
    std::vector<std::string> rows;
};

struct Blueprint {
    std::string id;
    std::string name;
    int grid_width{0};
    int grid_height{0};
    double pixel_scale_m{1.0};

    BlueprintLayer hit_layer;
    BlueprintLayer systems_layer;

    std::vector<Vec2> hull_polygon;
    double mass{0.0};
    Vec2 center_of_mass{0.0, 0.0};
    double moment_of_inertia{0.0};
    std::vector<EngineDescriptor> engines;
    std::vector<DockPortDescriptor> docking_ports;
};

class BlueprintLibrary {
public:
    void add(Blueprint bp) { blueprints_[bp.id] = std::move(bp); }

    const Blueprint* getBlueprint(const std::string& id) const {
        auto it = blueprints_.find(id);
        if (it == blueprints_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const std::unordered_map<std::string, Blueprint>& all() const { return blueprints_; }

private:
    std::unordered_map<std::string, Blueprint> blueprints_;
};

} // namespace orbital::core

