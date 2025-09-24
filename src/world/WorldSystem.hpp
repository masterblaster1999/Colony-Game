#pragma once
#include <cstdint>
#include "../procgen/ProceduralGraph.hpp"

namespace cg {

class WorldSystem {
public:
    explicit WorldSystem(const pg::Params& p) : params_(p) { rebuild(); }
    void rebuild() { outputs_ = pg::run_procedural_graph(params_); }

    pg::Params&       params()       { return params_; }
    const pg::Params& params() const { return params_; }
    const pg::Outputs& data()  const { return outputs_; }

private:
    pg::Params  params_;
    pg::Outputs outputs_;
};

} // namespace cg
