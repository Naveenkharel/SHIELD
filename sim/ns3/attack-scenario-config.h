#ifndef ATTACK_SCENARIO_CONFIG_H
#define ATTACK_SCENARIO_CONFIG_H

#include "nsfnet-topology.h"
#include "ns3/application-container.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "udp-flood-app.h"
#include "tcp-syn-flood-app.h"
#include "icmp-flood-app.h"

namespace ns3 {

enum class AttackFlavor { UDP_FLOOD, TCP_SYN_FLOOD, ICMP_FLOOD };

/**
 * Launch a DDoS attack flow from an external host attached at entryRouterId
 * toward targetRouterId.  The attack intensity is based on the last‑hop
 * link entering the target.  When entry == target, the access link is used.
 */
inline ApplicationContainer LaunchAttack(
    const Topology& topo,
    uint32_t entryRouterId,
    uint32_t targetRouterId,
    AttackFlavor flavor,
    double intensity,
    Time startTime,
    Time duration)
{
    NS_ASSERT(intensity > 0 && intensity <= 1.0);

    uint32_t lastHopNode;
    DataRate linkRate;

    // When the external host is directly attached to the target, the
    // bottleneck is the access link itself (1 Gbps).  Otherwise, use the
    // last backbone hop before the target.
    if (entryRouterId == targetRouterId)
    {
        lastHopNode = entryRouterId;
        linkRate = DataRate("1Gbps");       // access link rate
    }
    else
    {
        auto [last, rate] = GetLastHopInfo(topo, entryRouterId, targetRouterId);
        lastHopNode = last;
        linkRate = rate;
    }

    // Print the resolved last‑hop link and its DataRate
    std::cout << "[LaunchAttack] Entry " << entryRouterId
              << " -> Target " << targetRouterId
              << " : last‑hop link " << lastHopNode << " -> " << targetRouterId
              << ", DataRate = " << linkRate << std::endl;

    // Create the external attacker host
    Ptr<Node> hostNode = CreateExternalAttackerHost(topo, entryRouterId);

    // Destination address of the target router
    Ptr<Node> dstNode = topo.nodes.Get(targetRouterId);
    Ipv4Address dstAddr = dstNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    Address remote(InetSocketAddress(dstAddr, 9));

    ApplicationContainer app;
    if (flavor == AttackFlavor::UDP_FLOOD)
    {
        Ptr<UdpFloodApp> udpApp = CreateObject<UdpFloodApp>();
        udpApp->SetRemote(targetRouterId, remote);
        udpApp->SetIntensity(intensity, linkRate);
        hostNode->AddApplication(udpApp);
        udpApp->SetStartTime(startTime);
        udpApp->SetStopTime(startTime + duration);
        app.Add(udpApp);
    }
    else if (flavor == AttackFlavor::TCP_SYN_FLOOD)
    {
        Ptr<TcpSynFloodApp> synApp = CreateObject<TcpSynFloodApp>();
        synApp->SetRemote(targetRouterId, remote);
        synApp->SetIntensity(intensity, linkRate);
        hostNode->AddApplication(synApp);
        synApp->SetStartTime(startTime);
        synApp->SetStopTime(startTime + duration);
        app.Add(synApp);
    }
    else if (flavor == AttackFlavor::ICMP_FLOOD)
    {
        Ptr<IcmpFloodApp> icmpApp = CreateObject<IcmpFloodApp>();
        icmpApp->SetRemote(targetRouterId, remote);
        icmpApp->SetIntensity(intensity, linkRate);
        hostNode->AddApplication(icmpApp);
        icmpApp->SetStartTime(startTime);
        icmpApp->SetStopTime(startTime + duration);
        app.Add(icmpApp);
    }
    return app;
}

} // namespace ns3
#endif