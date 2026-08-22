#ifndef UDP_FLOOD_APP_H
#define UDP_FLOOD_APP_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/log.h"
#include "attack-tag.h"

namespace ns3 {

class UdpFloodApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("UdpFloodApp")
            .SetParent<Application>()
            .AddConstructor<UdpFloodApp>();
        return tid;
    }

    UdpFloodApp() : m_intensity(0.5), m_packetSize(1500) {}

    void SetRemote(uint32_t nodeId, const Address &remoteAddress)
    {
        m_remoteNodeId = nodeId;
        m_remote = remoteAddress;
    }

    void SetIntensity(double fraction, DataRate linkRate)
    {
        m_intensity = fraction;
        m_linkRate = linkRate;
        // Convert intensity to sending rate:
        // desired bitrate = fraction * linkRate (bps)
        // packet rate (pps) = desired bitrate / (packet_size * 8)
        uint64_t desiredBps = static_cast<uint64_t>(m_linkRate.GetBitRate() * m_intensity);
        double pps = desiredBps / (m_packetSize * 8.0);
        m_interval = Seconds(1.0 / pps);
    }

private:
    void StartApplication() override
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Connect(m_remote);
        SendPacket();
    }

    void StopApplication() override
    {
        if (m_socket) m_socket->Close();
        Simulator::Cancel(m_sendEvent);
    }

    void SendPacket()
    {
        Ptr<Packet> pkt = Create<Packet>(m_packetSize);
        AttackTag tag;
        tag.SetAttack(true, AttackType::UDP_FLOOD);
        pkt->AddPacketTag(tag);
        m_socket->Send(pkt);
        m_sendEvent = Simulator::Schedule(m_interval, &UdpFloodApp::SendPacket, this);
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