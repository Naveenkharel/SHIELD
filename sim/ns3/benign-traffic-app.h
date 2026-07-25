#ifndef BENIGN_TRAFFIC_APP_H
#define BENIGN_TRAFFIC_APP_H

#include "ns3/application.h"
#include "ns3/socket.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/log.h"
#include "attack-tag.h"
#include "nsfnet-topology.h"      // for Topology

namespace ns3 {

class BenignTrafficApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("BenignTrafficApp")
            .SetParent<Application>()
            .AddConstructor<BenignTrafficApp>();
        return tid;
    }

    BenignTrafficApp()
        : m_meanInterArrival(MilliSeconds(50)),
          m_sizeRng(nullptr),
          m_arrivalRng(CreateObject<ExponentialRandomVariable>())
    {}

    void SetRemote(uint32_t nodeId, const Address &remoteAddress)
    {
        m_remoteNodeId = nodeId;
        m_remote = remoteAddress;
    }

    void SetMeanInterArrival(Time mean) { m_meanInterArrival = mean; }

    void SetPacketSizeDistribution(Ptr<RandomVariableStream> rv) { m_sizeRng = rv; }

private:
    void StartApplication() override
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Connect(m_remote);
        m_arrivalRng->SetAttribute("Mean", DoubleValue(m_meanInterArrival.GetSeconds()));
        SendPacket();
    }

    void StopApplication() override
    {
        if (m_socket) m_socket->Close();
        Simulator::Cancel(m_sendEvent);
    }

    void SendPacket()
    {
        uint32_t size = m_sizeRng ? static_cast<uint32_t>(m_sizeRng->GetValue()) : 512;
        Ptr<Packet> pkt = Create<Packet>(size);
        AttackTag tag;
        tag.SetAttack(false);
        pkt->AddPacketTag(tag);
        m_socket->Send(pkt);

        Time delay = Seconds(m_arrivalRng->GetValue());
        m_sendEvent = Simulator::Schedule(delay, &BenignTrafficApp::SendPacket, this);
    }

    Ptr<Socket> m_socket;
    Address m_remote;
    uint32_t m_remoteNodeId;
    Time m_meanInterArrival;
    Ptr<RandomVariableStream> m_sizeRng;
    Ptr<ExponentialRandomVariable> m_arrivalRng;
    EventId m_sendEvent;
};

/**
 * Install a given number of benign flows on randomly chosen (src, dst) pairs.
 * \param topo      the network topology
 * \param numFlows  how many background flows to create
 * \param meanRateMs mean inter‑arrival time (milliseconds) for exponential distribution
 * \param rng       uniform random variable for reproducibility (used to pick node IDs)
 */
inline void SeedBackgroundTraffic(const Topology& topo,
                                  uint32_t numFlows,
                                  double meanRateMs,
                                  Ptr<UniformRandomVariable> rng)
{
    uint32_t nNodes = topo.nodes.GetN();
    for (uint32_t i = 0; i < numFlows; ++i)
    {
        uint32_t srcId = static_cast<uint32_t>(rng->GetValue(0, nNodes - 1));
        uint32_t dstId = static_cast<uint32_t>(rng->GetValue(0, nNodes - 1));
        if (srcId == dstId)
            dstId = (dstId + 1) % nNodes;

        Ptr<Node> srcNode = topo.nodes.Get(srcId);
        Ptr<Node> dstNode = topo.nodes.Get(dstId);
        Ipv4Address dstAddr = dstNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
        Address remote(InetSocketAddress(dstAddr, 9));

        Ptr<BenignTrafficApp> app = CreateObject<BenignTrafficApp>();
        app->SetRemote(dstId, remote);
        app->SetMeanInterArrival(MilliSeconds(meanRateMs));

        Ptr<UniformRandomVariable> sizeRng = CreateObject<UniformRandomVariable>();
        sizeRng->SetAttribute("Min", DoubleValue(64));
        sizeRng->SetAttribute("Max", DoubleValue(1500));
        app->SetPacketSizeDistribution(sizeRng);

        srcNode->AddApplication(app);
        Time start = Seconds(rng->GetValue(0.0, 2.0));
        app->SetStartTime(start);
        app->SetStopTime(Seconds(60.0));
    }
}

} // namespace ns3
#endif