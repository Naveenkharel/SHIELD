#ifndef ENFORCEMENT_STATE_H
#define ENFORCEMENT_STATE_H

#include <array>

namespace ns3 {

// Current action per router (0‑13). Values: 0=Allow, 1=RateLimit, 2=Throttle, 3=Drop, 4=Reroute.
inline std::array<int, 14> g_enforcementState = {0};

} // namespace ns3

#endif