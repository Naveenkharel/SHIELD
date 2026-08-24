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
    // Counters for the CURRENT 100 ms window
    uint32_t arrivedLegit  = 0;   // pre‑enforcement legitimate arrivals
    uint32_t arrivedAttack = 0;   // pre‑enforcement attack arrivals
    uint32_t legitDropped  = 0;   // legitimate drops (enforcement or overflow)
    uint32_t attackDropped = 0;   // attack drops (enforcement or overflow)
};

static std::vector<RouterRewardState> g_rewardState;

// ===================================================================
// Drop callback – fires when a packet is dropped by the queue disc
// ===================================================================
static void DropCallback(uint32_t nodeId, Ptr<const QueueDiscItem> item)
{
    if (IsAttackPacket(item->GetPacket()))
        g_rewardState[nodeId].attackDropped++;
    else
        g_rewardState[nodeId].legitDropped++;
}

// ===================================================================
// IPv4 arrival callback – reads AttackTag for pre‑enforcement counts.
// This is the ONLY place besides reward_utils that reads the tag.
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

    // Connect a separate IPv4 arrival trace on every node
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Ipv4> ipv4 = topo.nodes.Get(i)->GetObject<Ipv4>();
        ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&Ipv4ArrivalCallback));
    }
}

// ===================================================================
// Reward weights (tune as needed)
// ===================================================================
constexpr double W_THROUGHPUT = 1.0;
constexpr double W_FP_PENALTY = 3.0;

// ===================================================================
// ComputeReward – returns a scalar team reward for the current window
// ===================================================================
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

        // -----------------------------------------------------------------
        // Component A: legitimate throughput ratio
        // arrivedLegit = total legitimate packets that arrived this window
        // droppedLegit = legitimate packets that were dropped
        // If no legitimate traffic arrived at this router, skip it entirely
        // (no information about throughput quality).
        // -----------------------------------------------------------------
        uint32_t arrivedLegit = s.arrivedLegit;
        if (arrivedLegit > 0)
        {
            uint32_t droppedLegit = s.legitDropped;
            uint32_t legitSuccess = (arrivedLegit >= droppedLegit)
                                        ? (arrivedLegit - droppedLegit)
                                        : 0;
            double legitRatio = static_cast<double>(legitSuccess) / arrivedLegit;
            throughputReward += legitRatio;
            loadedRouters++;
        }

        // -----------------------------------------------------------------
        // Component B: false‑positive penalty
        // Only for routers that took an aggressive action this cycle.
        // Penalty = legitimate fraction of arriving traffic (pre‑enforcement).
        // If nothing arrived, penalty = 0.0 (no traffic to be false‑positive about).
        // -----------------------------------------------------------------
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
                fpPenaltyTotal += 1.0;   // aggressive + zero arriving traffic = fully unjustified
            }
            aggressiveRouters++;
        }
    }

    // Average Component A over routers that saw legitimate traffic.
    // If none did, default to 1.0 (neutral – nothing was lost because
    // there was nothing to lose).
    double avgThroughput = (loadedRouters > 0)
                               ? (throughputReward / loadedRouters)
                               : 1.0;

    // Average Component B over routers that took an aggressive action.
    // If none did, penalty is 0.0.
    double avgFpPenalty = (aggressiveRouters > 0)
                              ? (fpPenaltyTotal / aggressiveRouters)
                              : 0.0;

    double reward = W_THROUGHPUT * avgThroughput - W_FP_PENALTY * avgFpPenalty;

    // Reset all window counters for the next cycle
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        g_rewardState[i] = RouterRewardState();
    }

    return reward;
}

} // namespace ns3

#endif