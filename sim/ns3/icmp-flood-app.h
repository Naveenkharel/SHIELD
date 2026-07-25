#ifndef ICMP_FLOOD_APP_H
#define ICMP_FLOOD_APP_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/icmpv4.h"
#include "ns3/uinteger.h"
#include "attack-tag.h"

namespace ns3 {

class IcmpFloodApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("IcmpFloodApp")
            .SetParent<Application>()
            .AddConstructor<IcmpFloodApp>();
        return tid;
    }

    IcmpFloodApp() : m_intensity(0.5), m_packetSize(32) {} // 8 ICMP header + 24 data

    void SetRemote(uint32_t nodeId, const Address &remoteAddress)
    {
        m_remoteNodeId = nodeId;
        m_remote = remoteAddress;
    }

    void SetIntensity(double fraction, DataRate linkRate)
    {
        m_intensity = fraction;
        m_linkRate = linkRate;
        uint64_t desiredBps = static_cast<uint64_t>(m_linkRate.GetBitRate() * m_intensity);
        double pps = desiredBps / (m_packetSize * 8.0);
        m_interval = Seconds(1.0 / pps);
    }

private:
    void StartApplication() override
    {
        m_socket = Socket::CreateSocket(GetNode(), TypeId::LookupByName("ns3::Ipv4RawSocketFactory"));
        m_socket->SetAttribute("Protocol", UintegerValue(1)); // ICMP
        m_socket->Connect(m_remote);
        SendEcho();
    }

    void StopApplication() override
    {
        if (m_socket) m_socket->Close();
        Simulator::Cancel(m_sendEvent);
    }

    void SendEcho()
    {
        Ptr<Packet> pkt = Create<Packet>(24); // payload
        Icmpv4Echo echo;
        echo.SetSequenceNumber(0);
        echo.SetIdentifier(0);
        pkt->AddHeader(echo);

        Icmpv4Header icmpHeader;
        icmpHeader.SetType(Icmpv4Header::ICMPV4_ECHO);
        icmpHeader.SetCode(0);
        pkt->AddHeader(icmpHeader);

        AttackTag tag;
        tag.SetAttack(true, AttackType::ICMP_FLOOD);
        pkt->AddPacketTag(tag);

        m_socket->Send(pkt);
        m_sendEvent = Simulator::Schedule(m_interval, &IcmpFloodApp::SendEcho, this);
    }

    Ptr<Socket> m_socket;
    Address m_remote;
    uint32_t m_remoteNodeId;
    DataRate m_linkRate;
    double m_intensity;
    uint32_t m_packetSize;
    Time m_interval;
    EventId m_sendEvent;
};

} // namespace ns3
#endif