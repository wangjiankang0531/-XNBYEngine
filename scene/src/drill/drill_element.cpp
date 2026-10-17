#include <hongtu/scene/drill/drill_element.hpp>

namespace hongtu::scene::drill {

const char* toString(ElementKind k) {
  switch (k) {
    case ElementKind::Person:   return "person";
    case ElementKind::Vehicle:  return "vehicle";
    case ElementKind::Route:    return "route";
    case ElementKind::Region:   return "region";
    case ElementKind::Disaster: return "disaster";
    case ElementKind::Alarm:    return "alarm";
    case ElementKind::Marker:   return "marker";
  }
  return "marker";
}

bool kindFromString(const std::string& s, ElementKind& out) {
  if (s == "person")   { out = ElementKind::Person;   return true; }
  if (s == "vehicle")  { out = ElementKind::Vehicle;  return true; }
  if (s == "route")    { out = ElementKind::Route;    return true; }
  if (s == "region")   { out = ElementKind::Region;   return true; }
  if (s == "disaster") { out = ElementKind::Disaster; return true; }
  if (s == "alarm")    { out = ElementKind::Alarm;    return true; }
  if (s == "marker")   { out = ElementKind::Marker;   return true; }
  return false;
}

nlohmann::json DrillElement::toJson() const {
  nlohmann::json j;
  j["id"]      = id;
  j["kind"]    = toString(kind);
  j["name"]    = name;
  j["pos"]     = {pos.x, pos.y, pos.z};
  j["yaw_rad"] = yaw_rad;
  j["props"]   = props;
  if (moving) {
    nlohmann::json pts = nlohmann::json::array();
    for (const auto& p : route) pts.push_back({p.x, p.y, p.z});
    j["motion"] = {{"route", std::move(pts)},
                   {"progress_m", route_progress_m},
                   {"speed_mps", speed_mps}};
  }
  return j;
}

}  // namespace hongtu::scene::drill
