#ifndef TELEMETRY_COLLECTOR_H
#define TELEMETRY_COLLECTOR_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-header.h"
#include "nsfnet-topology.h"
#include "action-enforcer.h"          // custom queue disc

#include <vector>
#include <map>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace ns3 {

// ===================================================================
// Tunable constants – easy to defend in project documentation
// ===================================================================
constexpr uint32_t ASSUMED_AVG_PACKET_SIZE_BITS = 8000;   // 1000 bytes
constexpr uint32_t ASSUMED_MAX_CONCURRENT_FLOWS   = 20;
constexpr double   TELEMETRY_WINDOW_SEC           = 0.1;

// ===================================================================
// Per‑router telemetry state
// ===================================================================
struct RouterTelemetryState
{
    using FlowKey = std::pair<uint32_t, uint32_t>;   // (srcNodeId, dstNodeId)

    std::map<FlowKey, uint32_t> flowPacketCounts;    // current window
    uint32_t totalPacketsInWindow = 0;

    uint32_t queueAtWindowStart = 0;                // occupancy at window start
};

// ===================================================================
// Global state (indexed by node ID 0..13)
// ===================================================================
static std::vector<RouterTelemetryState> g_telemetryState;
static std::map<Ipv4Address, uint32_t>   g_ipToNodeId;
static std::vector<double>               g_maxPlausiblePacketsPerWindow;
static std::vector<uint32_t>             g_nodeQueueCapacity;

// ===================================================================
// IPv4 receive callback – fires for EVERY IPv4 packet arriving at a node.
// Signature matches Ipv4L3Protocol::Rx trace: void(Ptr<const Packet>, Ptr<Ipv4>, uint32_t interface)
//
// THIS CODE NEVER READS AttackTag OR INCLUDES reward_utils.h.
// ===================================================================
static void Ipv4RxCallback(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    // Identify the receiving node
    Ptr<Node> node = ipv4->GetObject<Node>();
    uint32_t nodeId = node->GetId();

    // Extract source and destination addresses
    Ipv4Header ipHeader;
    packet->PeekHeader(ipHeader);
    Ipv4Address srcAddr = ipHeader.GetSource();
    Ipv4Address dstAddr = ipHeader.GetDestination();

    auto srcIt = g_ipToNodeId.find(srcAddr);
    auto dstIt = g_ipToNodeId.find(dstAddr);
    if (srcIt == g_ipToNodeId.end() || dstIt == g_ipToNodeId.end())
        return;

    uint32_t srcNode = srcIt->second;
    uint32_t dstNode = dstIt->second;

    RouterTelemetryState::FlowKey flow(srcNode, dstNode);
    g_telemetryState[nodeId].flowPacketCounts[flow]++;
    g_telemetryState[nodeId].totalPacketsInWindow++;
}

// ===================================================================
// One‑time initialisation – builds IP map, connects traces, and
// pre‑computes per‑node constants.
// ===================================================================
inline void InitializeTelemetry(const Topology& topo)
{
    uint32_t nNodes = topo.nodes.GetN();
    g_telemetryState.resize(nNodes);
    g_maxPlausiblePacketsPerWindow.resize(nNodes, 0.0);
    g_nodeQueueCapacity.resize(nNodes, 0);
    g_ipToNodeId.clear();

    // --- Build IP → node ID map ---
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
        {
            for (uint32_t addrIndex = 0; addrIndex < ipv4->GetNAddresses(ifIndex); ++addrIndex)
            {
                Ipv4Address addr = ipv4->GetAddress(ifIndex, addrIndex).GetLocal();
                g_ipToNodeId[addr] = i;
            }
        }
    }

    // --- Install ActionEnforcingQueueDisc on every point‑to‑point device ---
    //     Uninstall any existing root queue disc first to avoid "already having one" error.
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        for (uint32_t devIdx = 0; devIdx < node->GetNDevices(); ++devIdx)
        {
            Ptr<NetDevice> dev = node->GetDevice(devIdx);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            if (p2pDev)
            {
                TrafficControlHelper tchUninstall;
                tchUninstall.Uninstall(dev);
                TrafficControlHelper tch;
                tch.SetRootQueueDisc("ns3::ActionEnforcingQueueDisc",
                                     "NodeId", UintegerValue(i));
                tch.Install(dev);
            }
        }
    }

    // --- Compute per‑node queue capacity and max plausible arrivals ---
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        uint32_t totalQueueCapacity = 0;
        double totalIncomingBps = 0.0;

        for (uint32_t devIdx = 0; devIdx < node->GetNDevices(); ++devIdx)
        {
            Ptr<NetDevice> dev = node->GetDevice(devIdx);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            if (!p2pDev) continue;

            Ptr<TrafficControlLayer> tcLayer = node->GetObject<TrafficControlLayer>();
            Ptr<QueueDisc> qdisc = tcLayer->GetRootQueueDiscOnDevice(dev);
            if (qdisc)
                totalQueueCapacity += qdisc->GetMaxSize().GetValue();

            DataRateValue rateVal;
            dev->GetAttribute("DataRate", rateVal);
            totalIncomingBps += rateVal.Get().GetBitRate();
        }

        g_nodeQueueCapacity[i] = totalQueueCapacity;
        double maxPps = totalIncomingBps / ASSUMED_AVG_PACKET_SIZE_BITS;
        g_maxPlausiblePacketsPerWindow[i] = maxPps * TELEMETRY_WINDOW_SEC;
    }

    // --- Connect IPv4 Rx traces on every node ---
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Ipv4> ipv4 = topo.nodes.Get(i)->GetObject<Ipv4>();
        ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&Ipv4RxCallback));
    }

    // --- Initialise window‑start queue values ---
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        uint32_t currentOccupancy = 0;
        Ptr<TrafficControlLayer> tcLayer = node->GetObject<TrafficControlLayer>();
        for (uint32_t devIdx = 0; devIdx < node->GetNDevices(); ++devIdx)
        {
            Ptr<NetDevice> dev = node->GetDevice(devIdx);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            if (!p2pDev) continue;
            Ptr<QueueDisc> qdisc = tcLayer->GetRootQueueDiscOnDevice(dev);
            if (qdisc)
                currentOccupancy += qdisc->GetNPackets();
        }
        g_telemetryState[i].queueAtWindowStart = currentOccupancy;
    }
}

// ===================================================================
// ComputeObservation – returns a 14×12 array of normalised metrics.
// Can be called at any point during the simulation.
// ===================================================================
inline std::vector<std::vector<double>> ComputeObservation(const Topology& topo)
{
    uint32_t nNodes = topo.nodes.GetN();
    std::vector<std::vector<double>> obs(nNodes, std::vector<double>(12, 0.0));

    for (uint32_t i = 0; i < nNodes; ++i)
    {
        Ptr<Node> node = topo.nodes.Get(i);
        uint32_t currentQueue = 0;
        Ptr<TrafficControlLayer> tcLayer = node->GetObject<TrafficControlLayer>();

        for (uint32_t devIdx = 0; devIdx < node->GetNDevices(); ++devIdx)
        {
            Ptr<NetDevice> dev = node->GetDevice(devIdx);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            if (!p2pDev) continue;
            Ptr<QueueDisc> qdisc = tcLayer->GetRootQueueDiscOnDevice(dev);
            if (qdisc)
                currentQueue += qdisc->GetNPackets();
        }

        uint32_t capacity        = g_nodeQueueCapacity[i];
        uint32_t windowStartQueue = g_telemetryState[i].queueAtWindowStart;
        uint32_t totalPkts        = g_telemetryState[i].totalPacketsInWindow;

        double qlen = (capacity > 0)
            ? std::min(1.0, static_cast<double>(currentQueue) / capacity) : 0.0;

        double growth = (capacity > 0)
            ? std::min(1.0, std::max(0.0,
                (static_cast<double>(currentQueue) - windowStartQueue + capacity) / (2.0 * capacity)))
            : 0.5;

        double arrival = (g_maxPlausiblePacketsPerWindow[i] > 0)
            ? std::min(1.0, totalPkts / g_maxPlausiblePacketsPerWindow[i]) : 0.0;

        double entropy = 0.0;
        if (totalPkts > 0)
        {
            for (const auto& kv : g_telemetryState[i].flowPacketCounts)
            {
                double p = static_cast<double>(kv.second) / totalPkts;
                entropy -= p * std::log2(p);
            }
            entropy /= std::log2(static_cast<double>(ASSUMED_MAX_CONCURRENT_FLOWS));
            entropy = std::min(1.0, entropy);
        }

        double hhi = 0.0;
        if (totalPkts > 0)
        {
            for (const auto& kv : g_telemetryState[i].flowPacketCounts)
            {
                double p = static_cast<double>(kv.second) / totalPkts;
                hhi += p * p;
            }
        }

        obs[i][0] = qlen;
        obs[i][1] = growth;
        obs[i][2] = arrival;
        obs[i][3] = entropy;
        obs[i][4] = hhi;
        obs[i][5] = 0.0;

        // Reset for next window
        g_telemetryState[i].flowPacketCounts.clear();
        g_telemetryState[i].totalPacketsInWindow = 0;
        g_telemetryState[i].queueAtWindowStart = currentQueue;
    }

    // --- Step 2: fill neighbour fields (6–11) ---
    for (uint32_t i = 0; i < nNodes; ++i)
    {
        auto it = topo.neighborMap.find(i);
        if (it == topo.neighborMap.end()) continue;

        std::vector<uint32_t> neighbors;
        for (const auto& nb : it->second)
            neighbors.push_back(nb.neighborId);
        std::sort(neighbors.begin(), neighbors.end());

        for (size_t idx = 0; idx < 3 && idx < neighbors.size(); ++idx)
        {
            uint32_t nbId = neighbors[idx];
            obs[i][6 + idx] = obs[nbId][0];   // neighbour's queue level
            obs[i][9 + idx] = obs[nbId][2];   // neighbour's arrival rate
        }
    }

    return obs;
}

} // namespace ns3
#endif