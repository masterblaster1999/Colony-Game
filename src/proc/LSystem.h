#pragma once
#include <string>
#include <unordered_map>
inline std::string LIterate(std::string s, const std::unordered_map<char,std::string>& R, int n){
    while(n--){
        std::string out; out.reserve(s.size()*2);
        for(char c: s){ auto it=R.find(c); out += (it!=R.end()? it->second : std::string(1,c)); }
        s.swap(out);
    }
    return s;
}
