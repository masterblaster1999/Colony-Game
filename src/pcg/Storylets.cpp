#include "Storylets.hpp"
#include <cmath>

namespace pcg {

static int cmp(const Value& a, const Value& b) {
    // compare numbers or strings; bools treated as ints
    auto toDouble = [](const Value& v)->double{
        if (auto p = std::get_if<int>(&v)) return *p;
        if (auto p = std::get_if<double>(&v)) return *p;
        if (auto p = std::get_if<bool>(&v)) return *p ? 1.0 : 0.0;
        return std::nan(""); // string: NaN
    };
    if (a.index()!=2 && b.index()!=2) {
        double da = toDouble(a), db = toDouble(b);
        if (std::isnan(da) || std::isnan(db)) return 0;
        if (da<db) return -1; if (da>db) return 1; return 0;
    } else {
        const std::string* sa = std::get_if<std::string>(&a);
        const std::string* sb = std::get_if<std::string>(&b);
        if (!sa || !sb) return 0;
        if (*sa < *sb) return -1; if (*sa > *sb) return 1; return 0;
    }
}

bool evaluate(const Storylet& s, const BlackBoard& bb) {
    for (const auto& pr : s.when) {
        auto it = bb.find(pr.key);
        if (it == bb.end()) return false;
        int c = cmp(it->second, pr.value);
        switch (pr.op) {
            case Op::LT: if (!(c<0)) return false; break;
            case Op::LE: if (!(c<=0)) return false; break;
            case Op::EQ: if (!(c==0)) return false; break;
            case Op::NE: if (!(c!=0)) return false; break;
            case Op::GE: if (!(c>=0)) return false; break;
            case Op::GT: if (!(c>0)) return false; break;
        }
    }
    return true;
}

void apply(const Storylet& s, BlackBoard& bb) {
    for (const auto& ef : s.effects) {
        auto& slot = bb[ef.key];
        if (ef.op == "+=") {
            if (auto v = std::get_if<int>(&slot))    *v += (int)std::get<double>(Value(ef.value.index()==0? (double)std::get<int>(ef.value) :
                                                          ef.value.index()==1? std::get<double>(ef.value) :
                                                          ef.value.index()==3? (std::get<bool>(ef.value)?1:0) : 0.0));
            else if (auto v = std::get_if<double>(&slot)) *v += (ef.value.index()==0? (double)std::get<int>(ef.value) :
                                                                 ef.value.index()==1? std::get<double>(ef.value) : 0.0);
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
    if (s=="<") return Op::LT; if (s=="<=") return Op::LE; if (s=="==") return Op::EQ;
    if (s=="!=") return Op::NE; if (s==">=") return Op::GE; return Op::GT;
}
static Value toVal(const YAML::Node& n) {
    if (n.IsScalar()) {
        // try int, then double, then string
        try { return n.as<int>();    } catch(...) {}
        try { return n.as<double>(); } catch(...) {}
        try { return n.as<std::string>(); } catch(...) {}
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
                p.key = w[0].as<std::string>();
                p.op  = parseOp(w[1].as<std::string>());
                p.value = toVal(w[2]);
                s.when.push_back(p);
            }
        }
        if (auto eff = root["effects"]; eff && eff.IsSequence()) {
            for (auto e : eff) {
                Effect ef;
                ef.key = e["key"].as<std::string>();
                ef.op  = e["op"].as<std::string>();
                ef.value = toVal(e["value"]);
                s.effects.push_back(ef);
            }
        }
        res.push_back(std::move(s));
    }
    return res;
}
#endif

} // namespace pcg
