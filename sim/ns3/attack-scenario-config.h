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
 * Launch a DDoS attack flow.
 * The intensity is a fraction of the last‑hop link’s capacity into the target.
 * Works for both adjacent and non‑adjacent attacker/target pairs.
 */
inline ApplicationContainer LaunchAttack(
    const Topology& topo,
    uint32_t targetNodeId,
    AttackFlavor flavor,
    double intensity,
    Time startTime,
    Time duration,
    uint32_t sourceNodeId = 0)
{
    NS_ASSERT(intensity > 0 && intensity <= 1.0);

    // Use the last‑hop link into the target (works for any pair)
    auto [lastHopNode, linkRate] = GetLastHopInfo(topo, sourceNodeId, targetNodeId);

    // Print the resolved last‑hop link and its DataRate for verification
    std::cout << "[LaunchAttack] Attacker " << sourceNodeId
              << " -> Target " << targetNodeId
              << " : last‑hop link " << lastHopNode << " -> " << targetNodeId
              << ", DataRate = " << linkRate << std::endl;

    Ptr<Node> srcNode = topo.nodes.Get(sourceNodeId);
    Ptr<Node> dstNode = topo.nodes.Get(targetNodeId);
    Ipv4Address dstAddr = dstNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    Address remote(InetSocketAddress(dstAddr, 9));

    ApplicationContainer app;
    if (flavor == AttackFlavor::UDP_FLOOD)
    {
        Ptr<UdpFloodApp> udpApp = CreateObject<UdpFloodApp>();
        udpApp->SetRemote(targetNodeId, remote);
        udpApp->SetIntensity(intensity, linkRate);
        srcNode->AddApplication(udpApp);
        udpApp->SetStartTime(startTime);
        udpApp->SetStopTime(startTime + duration);
        app.Add(udpApp);
    }
    else if (flavor == AttackFlavor::TCP_SYN_FLOOD)
    {
        Ptr<TcpSynFloodApp> synApp = CreateObject<TcpSynFloodApp>();
        synApp->SetRemote(targetNodeId, remote);
        synApp->SetIntensity(intensity, linkRate);
        srcNode->AddApplication(synApp);
        synApp->SetStartTime(startTime);
        synApp->SetStopTime(startTime + duration);
        app.Add(synApp);
    }
    else if (flavor == AttackFlavor::ICMP_FLOOD)
    {
        Ptr<IcmpFloodApp> icmpApp = CreateObject<IcmpFloodApp>();
        icmpApp->SetRemote(targetNodeId, remote);
        icmpApp->SetIntensity(intensity, linkRate);
        srcNode->AddApplication(icmpApp);
        icmpApp->SetStartTime(startTime);
        icmpApp->SetStopTime(startTime + duration);
        app.Add(icmpApp);
    }
    return app;
}

} // namespace ns3
#endif