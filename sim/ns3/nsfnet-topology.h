#ifndef NSFNET_TOPOLOGY_H
#define NSFNET_TOPOLOGY_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/ipv4-static-routing-helper.h"

#include <array>
#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace ns3 {

// ===================================================================
// Data structures for the NSFNET topology
// ===================================================================
struct NsfnetEdge
{
    uint32_t srcId;
    uint32_t dstId;
    std::string dataRate;
    std::string delay;
};

struct NeighborLinkInfo
{
    uint32_t neighborId;
    Ptr<PointToPointNetDevice> localDevice;
};

using NodeNeighborMap = std::map<uint32_t, std::vector<NeighborLinkInfo>>;

struct Topology
{
    NodeContainer nodes;
    NodeNeighborMap neighborMap;
    std::vector<NsfnetEdge> edges;
};

// ===================================================================
// Queue‑disc helpers (used by topology builder)
// ===================================================================
inline void SetQueueDiscOnDevice(Ptr<NetDevice> dev, const std::string& discType)
{
    TrafficControlHelper tch;
    tch.Uninstall(dev);
    tch.SetRootQueueDisc(discType);
    tch.Install(dev);
}

inline void SetQueueDiscOnNode(const NodeNeighborMap& map, uint32_t nodeId,
                               const std::string& discType)
{
    auto it = map.find(nodeId);
    NS_ASSERT_MSG(it != map.end(), "Node not found in neighbour map");
    for (const auto& nb : it->second)
        SetQueueDiscOnDevice(nb.localDevice, discType);
}

// ===================================================================
// Topology builder (no longer calls PopulateRoutingTables)
// ===================================================================
inline Topology BuildNsfnetTopology()
{
    static const std::array<NsfnetEdge, 21> edges = {{
        {0,  1,  "500Mbps",  "3.9ms"},
        {0,  2,  "1Gbps",    "2.1ms"},
        {0,  3,  "500Mbps",  "3.5ms"},
        {1,  2,  "155Mbps",  "5.7ms"},
        {1,  7,  "100Mbps",  "10.0ms"},
        {2,  5,  "155Mbps",  "7.1ms"},
        {3,  4,  "1Gbps",    "2.1ms"},
        {3,  10, "100Mbps",  "8.6ms"},
        {4,  5,  "500Mbps",  "3.9ms"},
        {4,  6,  "500Mbps",  "2.8ms"},
        {5,  8,  "500Mbps",  "4.2ms"},
        {5,  12, "155Mbps",  "7.1ms"},
        {6,  7,  "1Gbps",    "2.4ms"},
        {7,  9,  "1Gbps",    "2.4ms"},
        {8,  9,  "500Mbps",  "3.2ms"},
        {9,  11, "1Gbps",    "1.7ms"},
        {9,  13, "1Gbps",    "1.7ms"},
        {10, 11, "500Mbps",  "2.8ms"},
        {10, 13, "500Mbps",  "3.5ms"},
        {11, 12, "1Gbps",    "1.7ms"},
        {12, 13, "1Gbps",    "1.0ms"}
    }};

    Topology topo;
    topo.nodes.Create(14);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.252");
    InternetStackHelper internet;
    internet.Install(topo.nodes);

    for (const auto& edge : edges)
    {
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue(edge.dataRate));
        p2p.SetChannelAttribute("Delay", StringValue(edge.delay));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

        NetDeviceContainer devices = p2p.Install(topo.nodes.Get(edge.srcId),
                                                 topo.nodes.Get(edge.dstId));

        topo.neighborMap[edge.srcId].push_back(
            {edge.dstId, DynamicCast<PointToPointNetDevice>(devices.Get(0))});
        topo.neighborMap[edge.dstId].push_back(
            {edge.srcId, DynamicCast<PointToPointNetDevice>(devices.Get(1))});

        address.Assign(devices);
        address.NewNetwork();
        topo.edges.push_back(edge);
    }

    // Global routing is now called AFTER external hosts are added (in Reset())
    return topo;
}

// ===================================================================
// Topology summary printer
// ===================================================================
inline void PrintTopologySummary(const Topology& topo)
{
    std::cout << "Topology Summary\n";
    std::cout << "Total nodes: " << topo.nodes.GetN() << "\n";
    std::cout << "Total links: " << topo.edges.size() << "\n\n";
    std::cout << "Per-node neighbor lists:\n";
    for (uint32_t i = 0; i < topo.nodes.GetN(); ++i)
    {
        std::cout << "Node " << i << " neighbours: ";
        const auto& neighbors = topo.neighborMap.at(i);
        for (size_t j = 0; j < neighbors.size(); ++j)
        {
            std::cout << neighbors[j].neighborId;
            if (j < neighbors.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }
}

// ===================================================================
// BFS shortest‑path (hop count) and last‑hop info
// ===================================================================
inline std::vector<uint32_t>
FindShortestPath(const NodeNeighborMap& neighborMap, uint32_t src, uint32_t dst)
{
    std::unordered_map<uint32_t, uint32_t> parent;
    std::queue<uint32_t> q;
    std::unordered_set<uint32_t> visited;

    q.push(src);
    visited.insert(src);
    parent[src] = src;

    while (!q.empty())
    {
        uint32_t current = q.front();
        q.pop();
        if (current == dst)
        {
            std::vector<uint32_t> path;
            for (uint32_t v = dst; v != src; v = parent[v])
                path.push_back(v);
            path.push_back(src);
            std::reverse(path.begin(), path.end());
            return path;
        }
        auto it = neighborMap.find(current);
        if (it != neighborMap.end())
        {
            for (const auto& nb : it->second)
            {
                uint32_t next = nb.neighborId;
                if (visited.find(next) == visited.end())
                {
                    visited.insert(next);
                    parent[next] = current;
                    q.push(next);
                }
            }
        }
    }
    return {};
}

inline std::pair<uint32_t, DataRate>
GetLastHopInfo(const Topology& topo, uint32_t src, uint32_t dst)
{
    auto path = FindShortestPath(topo.neighborMap, src, dst);
    NS_ASSERT_MSG(!path.empty(), "No path between " << src << " and " << dst);
    NS_ASSERT_MSG(path.size() >= 2, "Path too short");

    uint32_t prevNode = path[path.size() - 2];
    uint32_t target   = dst;

    auto it = topo.neighborMap.find(prevNode);
    NS_ASSERT_MSG(it != topo.neighborMap.end(), "Node not in neighbour map");
    for (const auto& nb : it->second)
    {
        if (nb.neighborId == target)
        {
            StringValue rateStr;
            nb.localDevice->GetAttribute("DataRate", rateStr);
            DataRate rate(rateStr.Get());
            return {prevNode, rate};
        }
    }
    NS_FATAL_ERROR("Last‑hop link not found between " << prevNode << " and " << dst);
    return {0, DataRate(0)};
}

// ===================================================================
// External attacker host creation (NEW)
// ===================================================================
/**
 * Create an external host node connected to the specified entry router.
 * The host is NOT part of the 14‑defender set; it is a pure traffic source.
 *
 * Access link bandwidth/delay:
 *   Bandwidth = 1 Gbps  – typical for a well‑provisioned access link
 *   Delay     = 1 ms    – typical LAN / last‑mile value
 * These are representative numbers, not derived from NSFNET data, and are
 * intentionally generous so the access link is not the bottleneck.
 */
inline Ptr<Node> CreateExternalAttackerHost(const Topology& topo,
                                            uint32_t entryRouterId)
{
    Ptr<Node> hostNode = CreateObject<Node>();

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
    p2p.SetChannelAttribute("Delay", StringValue("1ms"));

    NetDeviceContainer devices = p2p.Install(hostNode, topo.nodes.Get(entryRouterId));

    InternetStackHelper internet;
    internet.Install(hostNode);

    // Assign a /30 subnet from a range that does not collide with backbone
    // addresses (backbone uses 10.0.0.0/30 blocks). We use 11.0.0.0/30.
    Ipv4AddressHelper addr;
    addr.SetBase("11.0.0.0", "255.255.255.252");
    Ipv4InterfaceContainer iface = addr.Assign(devices);

    // Add a static default route on the host pointing to the entry router
    Ipv4Address gatewayAddr = iface.GetAddress(1);   // entry router's side
    Ptr<Ipv4> hostIpv4 = hostNode->GetObject<Ipv4>();
    Ipv4StaticRoutingHelper staticRouting;
    Ptr<Ipv4StaticRouting> hostRouting = staticRouting.GetStaticRouting(hostIpv4);
    hostRouting->SetDefaultRoute(gatewayAddr, 1);

    return hostNode;
}

} // namespace ns3
#endif