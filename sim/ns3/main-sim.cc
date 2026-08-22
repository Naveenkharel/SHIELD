#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include "sim_controller.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <cassert>

using namespace ns3;

// ===================================================================
// Utility: assert observation shape is 14x12
// ===================================================================
static void AssertObsShape(const std::vector<std::vector<double>>& obs,
                           const std::string& context)
{
    bool ok = true;
    if (obs.size() != 14) {
        std::cerr << "FAIL [" << context << "]: outer dimension = "
                  << obs.size() << " (expected 14)\n";
        ok = false;
    }
    for (uint32_t i = 0; i < obs.size() && i < 14; ++i) {
        if (obs[i].size() != 12) {
            std::cerr << "FAIL [" << context << "]: node " << i
                      << " has " << obs[i].size() << " features (expected 12)\n";
            ok = false;
        }
    }
    if (!ok) std::exit(EXIT_FAILURE);
}

// ===================================================================
// Test mode: reproducibility – log scenario lines to file
// ===================================================================
static void RunReproducibilityTest(const std::string& outputFile, int numEpisodes)
{
    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Cannot open " << outputFile << "\n";
        return;
    }

    SimController controller;

    for (int ep = 0; ep < numEpisodes; ++ep) {
        // Redirect the controller's stdout temporarily to capture scenario
        // We can't easily capture what Reset() prints, so we'll print our own
        // summary line after reset. The scenario is already printed by Reset().
        auto obs = controller.Reset();

        // Write a clean one‑line scenario record
        // (The exact random values aren't exposed, but we know the episode
        //  sequence is deterministic. We record the observation checksum
        //  as a proxy for correctness.)
        double checksum = 0.0;
        for (auto& row : obs)
            for (double v : row) checksum += v;
        out << ep << " " << std::fixed << std::setprecision(6) << checksum << "\n";

        // Run full episode with default (all‑Allow) actions
        std::vector<int> actions(14, 0);
        for (int step = 0; step < 200; ++step) {
            auto result = controller.StepOnce(actions);
        }
    }
    out.close();
    std::cout << "Wrote " << numEpisodes << " episode records to " << outputFile << "\n";
}

// ===================================================================
// Test mode: benchmark wall‑clock time per episode
// ===================================================================
static void RunBenchmark(int numEpisodes)
{
    SimController controller;
    std::vector<double> times;

    for (int ep = 0; ep < numEpisodes; ++ep) {
        auto t0 = std::chrono::high_resolution_clock::now();

        auto obs = controller.Reset();

        std::vector<int> actions(14, 0);
        for (int step = 0; step < 200; ++step) {
            auto result = controller.StepOnce(actions);
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        times.push_back(ms);
        std::cout << "Episode " << ep << ": " << ms << " ms\n";
    }

    double total = 0.0, minT = times[0], maxT = times[0];
    for (double t : times) {
        total += t;
        if (t < minT) minT = t;
        if (t > maxT) maxT = t;
    }
    double avg = total / times.size();

    std::cout << "\n--- Benchmark summary (" << numEpisodes << " episodes) ---\n";
    std::cout << "Min  : " << minT << " ms\n";
    std::cout << "Max  : " << maxT << " ms\n";
    std::cout << "Avg  : " << avg << " ms\n";
    std::cout << "Estimated for 10,000 episodes: "
              << (avg * 10000 / 1000.0 / 60.0) << " minutes\n";
}

// ===================================================================
// Test mode: observation shape assertion across randomised episodes
// ===================================================================
static void RunShapeAssertionTest(int numEpisodes)
{
    SimController controller;

    for (int ep = 0; ep < numEpisodes; ++ep) {
        auto obs = controller.Reset();
        AssertObsShape(obs, "Reset ep " + std::to_string(ep));

        std::vector<int> actions(14, 0);
        for (int step = 0; step < 200; ++step) {
            auto result = controller.StepOnce(actions);
            AssertObsShape(result.obs,
                "StepOnce ep " + std::to_string(ep) + " step " + std::to_string(step));
        }
    }
    std::cout << "Shape assertion passed for " << numEpisodes << " episodes.\n";
}

// ===================================================================
// Combined test (default when no flags given)
// ===================================================================
static void RunAllTests()
{
    std::cout << "=== REPRODUCIBILITY CHECK ===\n";
    RunReproducibilityTest("episode_log_run1.txt", 10);

    std::cout << "\n=== BENCHMARK ===\n";
    RunBenchmark(10);

    std::cout << "\n=== SHAPE ASSERTION ===\n";
    RunShapeAssertionTest(10);
}

// ===================================================================
int main(int argc, char* argv[])
{
    std::string mode = "all";
    if (argc > 1) mode = argv[1];

    if (mode == "repro" || mode == "--repro") {
        std::string file = "episode_log.txt";
        if (argc > 2) file = argv[2];
        RunReproducibilityTest(file, 10);
    }
    else if (mode == "bench" || mode == "--bench") {
        int n = 10;
        if (argc > 2) n = std::stoi(argv[2]);
        RunBenchmark(n);
    }
    else if (mode == "shape" || mode == "--shape") {
        int n = 10;
        if (argc > 2) n = std::stoi(argv[2]);
        RunShapeAssertionTest(n);
    }
    else {
        RunAllTests();
    }

    return 0;
}