#include "Storylets.hpp"
#include <cmath>

namespace pcg {

static int cmp(const Value& a, const Value& b) {
    // Compare numbers or strings; bools treated as ints (1/0).
    // Non-string vs non-string: numeric compare.
    // String vs string: lexicographic.
    // Mixed string/non-string: treated as equal.

    const std::string* sa = std::get_if<std::string>(&a);
    const std::string* sb = std::get_if<std::string>(&b);
    const bool aIsStr = (sa != nullptr);
    const bool bIsStr = (sb != nullptr);

    if (!aIsStr && !bIsStr) {
        double da = 0.0;
        double db = 0.0;

        if (auto p = std::get_if<int>(&a))        da = static_cast<double>(*p);
        else if (auto p = std::get_if<double>(&a)) da = *p;
        else if (auto p = std::get_if<bool>(&a))   da = *p ? 1.0 : 0.0;

        if (auto p = std::get_if<int>(&b))        db = static_cast<double>(*p);
        else if (auto p = std::get_if<double>(&b)) db = *p;
        else if (auto p = std::get_if<bool>(&b))   db = *p ? 1.0 : 0.0;

        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }

    // At least one is a string. If both are strings, compare lexicographically;
    // if only one is a string, treat as equal (0), as before.
    if (!sa || !sb) {
        return 0;
    }

    if (*sa < *sb) return -1;
    if (*sa > *sb) return 1;
    return 0;
}

bool evaluate(const Storylet& s, const BlackBoard& bb) {
    for (const auto& pr : s.when) {
        auto it = bb.find(pr.key);
        if (it == bb.end()) return false;
        int c = cmp(it->second, pr.value);
        switch (pr.op) {
            case Op::LT: if (!(c <  0)) return false; break;
            case Op::LE: if (!(c <= 0)) return false; break;
            case Op::EQ: if (!(c == 0)) return false; break;
            case Op::NE: if (!(c != 0)) return false; break;
            case Op::GE: if (!(c >= 0)) return false; break;
            case Op::GT: if (!(c >  0)) return false; break;
        }
    }
    return true;
}

void apply(const Storylet& s, BlackBoard& bb) {
    for (const auto& ef : s.effects) {
        auto& slot = bb[ef.key];

        if (ef.op == "+=") {
            // Add numeric value of ef.value to an int/double slot.
            // Semantics preserved:
            //  - For int slot: int/double/bool (1/0) added; string treated as 0.
            //  - For double slot: int/double added; bool/string treated as 0.

            if (auto* slotInt = std::get_if<int>(&slot)) {
                double delta = 0.0;
                if (auto* p = std::get_if<int>(&ef.value)) {
                    delta = static_cast<double>(*p);
                } else if (auto* p = std::get_if<double>(&ef.value)) {
                    delta = *p;
                } else if (auto* p = std::get_if<bool>(&ef.value)) {
                    delta = *p ? 1.0 : 0.0;
                }
                *slotInt += static_cast<int>(delta);
            } else if (auto* slotDouble = std::get_if<double>(&slot)) {
                double delta = 0.0;
                if (auto* p = std::get_if<int>(&ef.value)) {
                    delta = static_cast<double>(*p);
                } else if (auto* p = std::get_if<double>(&ef.value)) {
                    delta = *p;
                }
                // bool/string: delta stays 0.0, matching previous behavior.
                *slotDouble += delta;
            }
        } else if (ef.op == "set") {
            slot = ef.value;
        } else if (ef.op == "unlock") {
            // no-op here; your game can listen for this effect
        }
    }
}

#ifdef PCG_USE_YAML
#include <yaml-cpp/yaml.h>
#include <filesystem>

static Op parseOp(const std::string& s) {
    if (s == "<")  return Op::LT;
    if (s == "<=") return Op::LE;
    if (s == "==") return Op::EQ;
    if (s == "!=") return Op::NE;
    if (s == ">=") return Op::GE;
    return Op::GT;
}

static Value toVal(const YAML::Node& n) {
    if (n.IsScalar()) {
        // Try int, then double, then string.
        try { return n.as<int>();           } catch (...) {}
        try { return n.as<double>();        } catch (...) {}
        try { return n.as<std::string>();   } catch (...) {}
    } else if (n.IsSequence() || n.IsMap()) {
        return n.as<std::string>(); // fallback
    }
    return 0;
}

std::vector<Storylet> load_storylets_from_dir(const std::string& dirPath) {
    std::vector<Storylet> res;
    for (auto& ent : std::filesystem::directory_iterator(dirPath)) {
        if (!ent.is_regular_file()) continue;
        auto path = ent.path();
        if (path.extension() != ".yaml" && path.extension() != ".yml") continue;

        YAML::Node root = YAML::LoadFile(path.string());
        Storylet s;

        s.id = root["id"].as<std::string>(path.stem().string());

        if (auto when = root["when"]; when && when.IsSequence()) {
            for (auto w : when) {
                Predicate p;
                p.key   = w[0].as<std::string>();
                p.op    = parseOp(w[1].as<std::string>());
                p.value = toVal(w[2]);
                s.when.push_back(std::move(p));
            }
        }

        if (auto eff = root["effects"]; eff && eff.IsSequence()) {
            for (auto e : eff) {
                Effect ef;
                ef.key   = e["key"].as<std::string>();
                ef.op    = e["op"].as<std::string>();
                ef.value = toVal(e["value"]);
                s.effects.push_back(std::move(ef));
            }
        }

        res.push_back(std::move(s));
    }
    return res;
}
#endif

} // namespace pcg
