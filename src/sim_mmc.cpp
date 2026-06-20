/*  sim_mmc.cpp  ─  M/M/c Queue: Effect of Number of Servers
 *
 *  Experiment 1: Fix λ=8, μ=2 (offered load a=4), vary c ∈ {1,2,4,6,8,12,16}
 *                → shows how adding cores reduces Wq and Lq
 *
 *  Experiment 2: Fix c=4, μ=3, vary λ from light (ρ=0.1) to heavy (ρ=0.95)
 *                → shows non-linear blow-up near saturation
 *
 *  Compile:
 *    g++ -O2 -std=c++17 -I../include sim_mmc.cpp -o sim_mmc
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "queue_models.h"
#include "csv_writer.h"

int main() {
    const int N_JOBS = 150000;

    // ── Experiment 1: vary number of servers ─────────────────────────────────
    std::cout << "=== Experiment 1: M/M/c — Varying Number of Servers ===\n";
    {
        double lambda = 8.0;
        double mu     = 2.0;   // per-server rate → a = λ/μ = 4

        std::vector<int> servers = {1, 2, 4, 6, 8, 12, 16};
        std::ofstream summary("../data/exp1_summary.csv");
        write_summary_header(summary);

        for (int c : servers) {
            double rho = lambda / (c * mu);
            if (rho >= 1.0) {
                std::cout << "  c=" << c << " UNSTABLE (ρ=" << rho << "), skip\n";
                continue;
            }
            std::cout << "  c=" << c << "  ρ=" << rho << " ... " << std::flush;
            MMcSimulator sim(c, lambda, mu, N_JOBS, 42 + c);
            SimResult r = sim.run("M/M/" + std::to_string(c));

            write_summary_row(summary, r);

            // Also write per-job CSV for c=2,4,8 (used for distribution plots)
            if (c == 2 || c == 4 || c == 8) {
                write_jobs_csv(r, "../data/exp1_jobs_c" + std::to_string(c) + ".csv");
                write_queue_ts_csv(r, "../data/exp1_qts_c" + std::to_string(c) + ".csv");
            }

            std::cout << "Wq_sim=" << r.mean_waiting_time
                      << "  Wq_theory=" << r.theory_Wq << "\n";
        }
        std::cout << "  → data/exp1_summary.csv\n\n";
    }

    // ── Experiment 2: vary utilization ───────────────────────────────────────
    std::cout << "=== Experiment 2: M/M/4 — Varying Utilization ===\n";
    {
        int    c  = 4;
        double mu = 3.0;

        // rho targets: 0.05 to 0.97
        std::vector<double> rhos = {0.05,0.10,0.20,0.30,0.40,0.50,
                                    0.60,0.70,0.80,0.85,0.90,0.93,0.95,0.97};
        std::ofstream summary("../data/exp2_summary.csv");
        write_summary_header(summary);

        for (double rho : rhos) {
            double lambda = rho * c * mu;
            std::cout << "  ρ=" << rho << "  λ=" << lambda << " ... " << std::flush;
            MMcSimulator sim(c, lambda, mu, N_JOBS, 123);
            SimResult r = sim.run("M/M/4_rho" + std::to_string(rho));
            write_summary_row(summary, r);

            if (rho == 0.50 || rho == 0.80 || rho == 0.93)
                write_jobs_csv(r, "../data/exp2_jobs_rho" +
                               std::to_string(static_cast<int>(rho*100)) + ".csv");

            std::cout << "Wq=" << r.mean_waiting_time
                      << " (theory=" << r.theory_Wq << ")\n";
        }
        std::cout << "  → data/exp2_summary.csv\n\n";
    }

    // ── Experiment 3: Scalability — fixed load per server ────────────────────
    // Demonstrates "load-balanced" scaling: if we double λ AND double c,
    // ρ stays constant but absolute throughput doubles.
    std::cout << "=== Experiment 3: Scalability Scaling ===\n";
    {
        double mu  = 2.0;
        double rho = 0.75;

        std::vector<int> cores = {1, 2, 4, 8, 16, 32};
        std::ofstream summary("../data/exp3_summary.csv");
        write_summary_header(summary);

        for (int c : cores) {
            double lambda = rho * c * mu;  // keep ρ fixed
            std::cout << "  c=" << c << "  λ=" << lambda << " ... " << std::flush;
            MMcSimulator sim(c, lambda, mu, N_JOBS, 7);
            SimResult r = sim.run("Scale_c" + std::to_string(c));
            write_summary_row(summary, r);
            std::cout << "throughput=" << r.throughput
                      << "  Wq=" << r.mean_waiting_time << "\n";
        }
        std::cout << "  → data/exp3_summary.csv\n\n";
    }

    std::cout << "All simulations complete.\n";
    return 0;
}
