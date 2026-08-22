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
        // then reward hooks are attached to those discs.
        InitializeTelemetry(m_topo);
        InitializeRewardState(m_topo);

        // Reproducible background traffic
        Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
        uv->SetAttribute("Min", DoubleValue(0));
        uv->SetAttribute("Max", DoubleValue(m_topo.nodes.GetN() - 1));
        SeedBackgroundTraffic(m_topo, 8, 50.0, uv);

        // --- Fully randomised attack scenario ---
        Ptr<UniformRandomVariable> entryRng = CreateObject<UniformRandomVariable>();
        entryRng->SetAttribute("Min", DoubleValue(0));
        entryRng->SetAttribute("Max", DoubleValue(m_topo.nodes.GetN() - 1));
        uint32_t entryRouter = static_cast<uint32_t>(entryRng->GetValue());

        Ptr<UniformRandomVariable> targetRng = CreateObject<UniformRandomVariable>();
        targetRng->SetAttribute("Min", DoubleValue(0));
        targetRng->SetAttribute("Max", DoubleValue(m_topo.nodes.GetN() - 1));
        uint32_t targetRouter = static_cast<uint32_t>(targetRng->GetValue());

        // Attack type: 0=UDP, 1=TCP SYN, 2=ICMP
        Ptr<UniformRandomVariable> typeRng = CreateObject<UniformRandomVariable>();
        typeRng->SetAttribute("Min", DoubleValue(0));
        typeRng->SetAttribute("Max", DoubleValue(2));
        int typeVal = static_cast<int>(typeRng->GetValue());
        AttackFlavor flavor;
        switch (typeVal) {
            case 0: flavor = AttackFlavor::UDP_FLOOD;       break;
            case 1: flavor = AttackFlavor::TCP_SYN_FLOOD;   break;
            case 2: flavor = AttackFlavor::ICMP_FLOOD;      break;
            default: flavor = AttackFlavor::TCP_SYN_FLOOD;   break;
        }

        // Intensity: uniform 10‑100% (0.1‑1.0)
        Ptr<UniformRandomVariable> intRng = CreateObject<UniformRandomVariable>();
        intRng->SetAttribute("Min", DoubleValue(0.1));
        intRng->SetAttribute("Max", DoubleValue(1.0));
        double intensity = intRng->GetValue();

        // Start time: uniform 3‑7 seconds
        Ptr<UniformRandomVariable> startRng = CreateObject<UniformRandomVariable>();
        startRng->SetAttribute("Min", DoubleValue(3.0));
        startRng->SetAttribute("Max", DoubleValue(7.0));
        double startTime = startRng->GetValue();

        // --- Guarantee a recovery window after the attack ---
        constexpr double EPISODE_SIM_SECONDS      = 20.0;   // 200 steps * 100 ms
        constexpr double RECOVERY_BUFFER_SECONDS  = 3.0;    // minimum time after attack ends
        constexpr double MIN_DURATION_SECONDS     = 5.0;    // shortest attack worth simulating

        double maxDuration = EPISODE_SIM_SECONDS - RECOVERY_BUFFER_SECONDS - startTime;
        // Since startTime is in [3,7], maxDuration ∈ [10,14].
        // MIN_DURATION_SECONDS is always ≤ maxDuration, so no clamping needed.

        Ptr<UniformRandomVariable> durRng = CreateObject<UniformRandomVariable>();
        durRng->SetAttribute("Min", DoubleValue(MIN_DURATION_SECONDS));
        durRng->SetAttribute("Max", DoubleValue(maxDuration));
        double duration = durRng->GetValue();

        double recoveryWindow = EPISODE_SIM_SECONDS - startTime - duration;
        // recoveryWindow is guaranteed ≥ RECOVERY_BUFFER_SECONDS.

        std::cout << "[Scenario] entry=" << entryRouter
                  << " target=" << targetRouter
                  << " type=" << typeVal
                  << " intensity=" << intensity
                  << " start=" << startTime << "s dur=" << duration << "s"
                  << " recovery=" << recoveryWindow << "s" << std::endl;

        LaunchAttack(m_topo, entryRouter, targetRouter, flavor,
                     intensity, Seconds(startTime), Seconds(duration));

        // Recompute global routes now that external host exists
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