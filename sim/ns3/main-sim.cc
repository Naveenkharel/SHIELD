#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "sim_controller.h"
#include <iostream>
#include <iomanip>

using namespace ns3;

int main(int argc, char* argv[])
{
    SimController controller;

    for (int ep = 0; ep < 2; ++ep)
    {
        std::cout << "\n========== Episode " << ep+1 << " ==========\n";
        auto obs = controller.Reset();
        std::cout << "Reset done.  Sim time = " << Simulator::Now().GetSeconds()
                  << " s, step 0 obs row[0][0] = " << obs[0][0] << "\n";

        std::vector<int> actions(14, 0);
        for (int step = 0; step < 200; ++step)
        {
            // Apply DROP on node 9 during steps 50‑79
            if (step >= 50 && step < 80)
                actions[9] = 3;
            else
                actions[9] = 0;

            auto result = controller.StepOnce(actions);

            std::cout << "Step " << std::setw(3) << step
                      << "  time=" << Simulator::Now().GetSeconds()
                      << "s  reward=" << result.reward
                      << "  done=" << result.done << "\n";
        }
        std::cout << "Episode finished at sim time " << Simulator::Now().GetSeconds() << " s\n";
    }

    Simulator::Destroy();
    return 0;
}