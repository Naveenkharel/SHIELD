#ifndef ACTION_ENFORCER_H
#define ACTION_ENFORCER_H

#include "ns3/traffic-control-module.h"
#include "enforcement_state.h"

namespace ns3 {

class ActionEnforcingQueueDisc : public QueueDisc
{
public:
    static TypeId GetTypeId();
    ActionEnforcingQueueDisc();

private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    uint32_t m_nodeId;
    Ptr<UniformRandomVariable> m_rng;
    std::deque<Ptr<QueueDiscItem>> m_queue;
};

// ---------- inline implementation ----------

inline TypeId ActionEnforcingQueueDisc::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ActionEnforcingQueueDisc")
        .SetParent<QueueDisc>()
        .SetGroupName("TrafficControl")
        .AddConstructor<ActionEnforcingQueueDisc>()
        .AddAttribute("NodeId",
                      "The node ID (0‑13) this disc belongs to.",
                      UintegerValue(0),
                      MakeUintegerAccessor(&ActionEnforcingQueueDisc::m_nodeId),
                      MakeUintegerChecker<uint32_t>());
    return tid;
}

// Required for ns‑3 to properly register the type at static initialisation
NS_OBJECT_ENSURE_REGISTERED(ActionEnforcingQueueDisc);

inline ActionEnforcingQueueDisc::ActionEnforcingQueueDisc()
    : m_nodeId(0),
      m_rng(CreateObject<UniformRandomVariable>())
{
    // Set a default maximum queue size (1000 packets) so GetMaxSize() returns a valid value
    SetMaxSize(QueueSize("1000p"));
    m_rng->SetAttribute("Min", DoubleValue(0.0));
    m_rng->SetAttribute("Max", DoubleValue(1.0));
}

inline bool ActionEnforcingQueueDisc::DoEnqueue(Ptr<QueueDiscItem> item)
{
    int action = g_enforcementState[m_nodeId];
    double keepProbability = 1.0;

    switch (action)
    {
    case 0: keepProbability = 1.0;  break;   // Allow
    case 1: keepProbability = 0.5;  break;   // Rate‑limit 50%
    case 2: keepProbability = 0.1;  break;   // Throttle 10%
    case 3: keepProbability = 0.0;  break;   // Drop flow
    case 4: keepProbability = 0.1;  break;   // Reroute (TODO)
    default: break;
    }

    // If the random decision says "drop", return false – the traffic‑control
    // layer will drop the packet and fire the Drop trace for the reward module.
    if (m_rng->GetValue() < keepProbability)
    {
        if (GetMaxSize().GetValue() > m_queue.size())
        {
            m_queue.push_back(item);
            return true;
        }
        return false;   // queue full
    }
    return false;       // intentional drop
}

inline Ptr<QueueDiscItem> ActionEnforcingQueueDisc::DoDequeue()
{
    if (!m_queue.empty())
    {
        Ptr<QueueDiscItem> item = m_queue.front();
        m_queue.pop_front();
        return item;
    }
    return nullptr;
}

inline Ptr<const QueueDiscItem> ActionEnforcingQueueDisc::DoPeek()
{
    return m_queue.empty() ? nullptr : m_queue.front();
}

inline bool ActionEnforcingQueueDisc::CheckConfig()
{
    return true;
}

inline void ActionEnforcingQueueDisc::InitializeParams()
{
}

} // namespace ns3

#endif