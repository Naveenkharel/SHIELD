#ifndef REWARD_COMPUTATION_H
#define REWARD_COMPUTATION_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/queue-disc.h"
#include "reward_utils.h"          // IsAttackPacket()
#include "nsfnet-topology.h"       // Topology, NodeNeighborMap

#include <vector>
#include <cstdint>
#include <iostream>

namespace ns3 {

enum class Action {
    NONE,
    RATE_LIMIT,
    THROTTLE,
    DROP,
    REROUTE
};

struct RouterRewardState
{
    // Arrival counters (pre‑enforcement) – updated via IPv4 Rx trace
    uint32_t arrivedLegit   = 0;
    uint32_t arrivedAttack  = 0;

    // Drop counters – updated via queue disc "Drop" trace
    uint32_t legitDropped   = 0;
    uint32_t attackDropped  = 0;
};

static std::vector<RouterRewardState> g_rewardState;

// ===================================================================
// Drop callback – unchanged
// ===================================================================
static void DropCallback(uint32_t nodeId, Ptr<const QueueDiscItem> item)
{
    if (IsAttackPacket(item->GetPacket()))
        g_rewardState[nodeId].attackDropped++;
    else
        g_rewardState[nodeId].legitDropped++;
}

// ===================================================================
// IPv4 arrival callback – reads AttackTag for pre‑enforcement counts
// ===================================================================
static void Ipv4ArrivalCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    Ptr<Node> node = ipv4->GetObject<Node>();
    uint32_t nodeId = node->GetId();

    if (IsAttackPacket(packet))
        g_rewardState[nodeId].arrivedAttack++;
    else
        g_rewardState[nodeId].arrivedLegit++;
}

// ===================================================================
// Initialization – MUST be called AFTER InitializeTelemetry()
// ===================================================================
inline void InitializeRewardState(const Topology& topo)
{
    uint32_t nNodes = topo.nodes.GetN();
    g_rewardState.resize(nNodes);

    // Connect Drop trace on every queue disc
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        for (uint32_t dIdx = 0; dIdx < node->GetNDevices(); ++dIdx)
        {
            Ptr<NetDevice> dev = node->GetDevice(dIdx);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            if (!p2pDev) continue;

            Ptr<TrafficControlLayer> tcLayer = node->GetObject<TrafficControlLayer>();
            Ptr<QueueDisc> qdisc = tcLayer->GetRootQueueDiscOnDevice(dev);
            if (!qdisc) continue;

            qdisc->TraceConnectWithoutContext("Drop",
                MakeBoundCallback(&DropCallback, i));
        }
    }

    // Connect IPv4 arrival trace on every node (same as telemetry, but separate)
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Ipv4> ipv4 = topo.nodes.Get(i)->GetObject<Ipv4>();
        ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&Ipv4ArrivalCallback));
    }
}

// ===================================================================
// Reward computation
// ===================================================================
constexpr double W_THROUGHPUT = 1.0;
constexpr double W_FP_PENALTY = 3.0;

inline double ComputeReward(const Topology& topo,
                            const std::vector<Action>& actions)
{
    double throughputReward = 0.0;
    int    loadedRouters    = 0;

    double fpPenaltyTotal   = 0.0;
    int    aggressiveRouters = 0;

    uint32_t nNodes = topo.nodes.GetN();

    for (uint32_t i = 0; i < nNodes; ++i)
    {
        const RouterRewardState& s = g_rewardState[i];

        // Component A: legitimate throughput ratio using arrival‑minus‑drops
        uint32_t legitArrived = s.arrivedLegit;
        uint32_t legitDropped = s.legitDropped;
        if (legitArrived > 0)
        {
            // Estimated successful legitimate packets = arrived - dropped (capped at 0)
            uint32_t legitSuccess = (legitArrived >= legitDropped) ? (legitArrived - legitDropped) : 0;
            double legitRatio = static_cast<double>(legitSuccess) / legitArrived;
            throughputReward += legitRatio;
            loadedRouters++;
        }

        // Component B: false‑positive penalty (pre‑enforcement arrival mix)
        if (actions[i] != Action::NONE)
        {
            uint32_t totalArrived = s.arrivedLegit + s.arrivedAttack;
            if (totalArrived > 0)
            {
                double legitFraction = static_cast<double>(s.arrivedLegit) / totalArrived;
                fpPenaltyTotal += legitFraction;
            }
            else
            {
                fpPenaltyTotal += 0.0;   // no traffic arrived, no false positive
            }
            aggressiveRouters++;
        }
    }

    double avgThroughput = (loadedRouters > 0) ? (throughputReward / loadedRouters) : 0.0;
    double avgFpPenalty  = (aggressiveRouters > 0) ? (fpPenaltyTotal / aggressiveRouters) : 0.0;

    double reward = W_THROUGHPUT * avgThroughput - W_FP_PENALTY * avgFpPenalty;

    // Reset all window counters
    for (uint32_t i = 0; i < nNodes; ++i)
        g_rewardState[i] = RouterRewardState();

    return reward;
}

} // namespace ns3
#endif