#ifndef SIM_CONTROLLER_H
#define SIM_CONTROLLER_H

#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "nsfnet-topology.h"
#include "telemetry_collector.h"
#include "reward_computation.h"
#include "attack-scenario-config.h"
#include "benign-traffic-app.h"
#include "enforcement_state.h"

#include <vector>
#include <iostream>
#include <memory>

namespace ns3 {

class SimController
{
public:
    SimController()
        : m_stepCount(0)
    {
        RngSeedManager::SetSeed(12345);
        RngSeedManager::SetRun(1);
    }

    std::vector<std::vector<double>> Reset()
    {
        Simulator::Destroy();
        m_topo = BuildNsfnetTopology();

        // ORDERING MATTERS: telemetry installs the final queue discs,
        // then reward hooks are attached to those discs.  Do NOT swap.
        InitializeTelemetry(m_topo);
        InitializeRewardState(m_topo);

        Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
        uv->SetAttribute("Min", DoubleValue(0));
        uv->SetAttribute("Max", DoubleValue(m_topo.nodes.GetN() - 1));
        SeedBackgroundTraffic(m_topo, 8, 50.0, uv);

        LaunchAttack(m_topo,
                     11,
                     AttackFlavor::TCP_SYN_FLOOD,
                     0.5,
                     Seconds(5.0),
                     Seconds(20.0),
                     1);

        Ipv4GlobalRoutingHelper::PopulateRoutingTables();

        m_stepCount = 0;
        m_stopEvent = EventId();
        return ComputeObservation(m_topo);
    }

    struct StepResult {
        std::vector<std::vector<double>> obs;
        double reward;
        bool done;
    };

    StepResult StepOnce(const std::vector<int>& rawActions)
    {
        NS_ASSERT(rawActions.size() == m_topo.nodes.GetN());

        for (uint32_t i = 0; i < rawActions.size(); ++i)
            g_enforcementState[i] = rawActions[i];

        std::vector<Action> actions(m_topo.nodes.GetN(), Action::NONE);
        for (uint32_t i = 0; i < rawActions.size(); ++i)
        {
            switch (rawActions[i]) {
            case 0: actions[i] = Action::NONE;       break;
            case 1: actions[i] = Action::RATE_LIMIT; break;
            case 2: actions[i] = Action::THROTTLE;   break;
            case 3: actions[i] = Action::DROP;       break;
            case 4: actions[i] = Action::REROUTE;    break;
            default: NS_ASSERT(false);               break;
            }
            if (actions[i] != Action::NONE)
            {
                std::cout << "[Action] Node " << i << " set to "
                          << static_cast<int>(actions[i]) << " (ENFORCED)" << std::endl;
            }
        }

        Simulator::Cancel(m_stopEvent);
        m_stopEvent = Simulator::Schedule(MilliSeconds(100), &Simulator::Stop);
        Simulator::Run();

        StepResult result;
        result.obs = ComputeObservation(m_topo);
        result.reward = ComputeReward(m_topo, actions);
        result.done = (++m_stepCount >= 200);
        return result;
    }

private:
    Topology m_topo;
    uint32_t m_stepCount;
    EventId m_stopEvent;
};

} // namespace ns3
#endif
