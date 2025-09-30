#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>

namespace pcg {

using Value = std::variant<int,double,std::string,bool>;

enum class Op { LT, LE, EQ, NE, GE, GT };

struct Predicate {
    std::string key; Op op; Value value;
};

struct Effect {
    std::string key; // e.g., "morale"
    std::string op;  // "+=", "set", "unlock"
    Value value;
};

struct Storylet {
    std::string id;
    std::vector<Predicate> when;
    std::vector<Effect> effects;
};

using BlackBoard = std::unordered_map<std::string, Value>;

bool evaluate(const Storylet& s, const BlackBoard& bb);
void apply(const Storylet& s, BlackBoard& bb);

#ifdef PCG_USE_YAML
std::vector<Storylet> load_storylets_from_dir(const std::string& dirPath);
#endif

} // namespace pcg
