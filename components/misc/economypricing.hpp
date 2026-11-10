#ifndef ARENAMP_ECONOMY_PRICING_HPP
#define ARENAMP_ECONOMY_PRICING_HPP
#include "crafteconomicprotocol.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
namespace ArenaEconomy {
inline double coefficient(const ArenaCraft::Value& value) {
    return value.type==ArenaCraft::Value::Number && std::isfinite(value.number)
        ? std::clamp(value.number,0.01,100.0) : 1.0;
}
inline double factor(const ArenaCraft::Value& policy,const std::string& service,std::string id,bool buying) {
    for (char& c:id) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto& item=policy.at("items").at(id);
    const auto& services=policy.at("services");
    const auto side=buying ? "buy" : "sell";
    double f=coefficient(policy.at(side))*coefficient(services.at(service))*coefficient(item.at(side));
    if (!buying) f=std::min(f,coefficient(policy.at("buy"))*coefficient(services.at(service))*coefficient(item.at("buy")));
    return std::clamp(f,0.01,100.0);
}
inline int price(int base,const ArenaCraft::Value& policy,const std::string& service,const std::string& id,bool buying) {
    if (base<=0) return 0;
    const double value=std::clamp(base*factor(policy,service,id,buying),0.0,1000000000.0);
    return static_cast<int>(buying ? std::ceil(value-1e-8) : std::floor(value+1e-8));
}
}
#endif
