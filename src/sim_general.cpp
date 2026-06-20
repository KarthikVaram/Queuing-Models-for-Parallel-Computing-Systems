/*  sim_general.cpp  ─  General Service Time Distributions
 *
 *  Compares M/M/c vs M/D/c vs M/Ek/2-c vs M/H2/c at equal utilisation.
 *  The service-time distribution affects Wq via the Kingman / Allen-Cunneen
 *  heavy-traffic approximation:
 *
 *    Wq(G) ≈ Wq(M/M/c) × (Ca² + Cs²) / 2
 *
 *  where Ca=1 for Poisson arrivals and Cs² is the squared coefficient
 *  of variation of service times (Cs²=1 Exp, Cs²=0 Det, Cs²=(k-1)/k Erlang-k).
 *
 *  Compile:
 *    g++ -O2 -std=c++17 -I../include sim_general.cpp -o sim_general
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <functional>
#include <string>
#include <numeric>
#include <cmath>
#include "queue_models.h"
#include "csv_writer.h"

// Generic simulator: service time drawn from `svc_dist`
SimResult simulate_general(int c, double lambda,
                            std::function<double()> svc_dist,
                            double mu_mean,  // 1/E[S]
                            int n_jobs, unsigned seed,
                            const std::string& name) {
    struct Event {
        double time; int type; int server;
        bool operator>(const Event& o) const { return time > o.time; }
    };

    RNG rng(seed);
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> pq;
    std::vector<double> server_free_at(c, 0.0);
    std::queue<double>  wq_arr;   // arrival times of waiting jobs
    std::queue<double>  wq_svc;   // their pre-drawn service times
    std::vector<Job>    completed;
    completed.reserve(n_jobs);

    std::vector<double> q_times, q_lengths;
    double sample_dt = 1.0 / lambda * 0.5;
    double next_s    = 0.0;
    int    queue_len = 0;
    int    in_system = 0;
    int    next_id   = 0;
    int    departed  = 0;

    pq.push({rng.exponential(lambda), 0, -1});

    while (departed < n_jobs) {
        if (pq.empty()) break;
        auto ev = pq.top(); pq.pop();
        double t = ev.time;

        while (next_s <= t) {
            q_times.push_back(next_s);
            q_lengths.push_back(static_cast<double>(queue_len));
            next_s += sample_dt;
        }

        if (ev.type == 0) {
            // ARRIVAL
            ++next_id; ++in_system;
            queue_len = std::max(0, in_system - c);
            double svc = svc_dist();

            int free_srv = -1;
            for (int i = 0; i < c; ++i)
                if (server_free_at[i] <= t) { free_srv = i; break; }

            if (free_srv >= 0) {
                double dep = t + svc;
                server_free_at[free_srv] = dep;
                Job j; j.id=next_id; j.arrival_time=t;
                j.service_start=t; j.departure_time=dep; j.server_id=free_srv;
                completed.push_back(j);
                pq.push({dep, 1, free_srv});
            } else {
                wq_arr.push(t); wq_svc.push(svc);
            }

            if (next_id < n_jobs * 2)
                pq.push({t + rng.exponential(lambda), 0, -1});

        } else {
            // DEPARTURE
            ++departed; --in_system;
            queue_len = std::max(0, in_system - c);

            if (!wq_arr.empty()) {
                double arr = wq_arr.front(); wq_arr.pop();
                double svc = wq_svc.front(); wq_svc.pop();
                double dep = t + svc;
                server_free_at[ev.server] = dep;
                Job j; j.id=++next_id; j.arrival_time=arr;
                j.service_start=t; j.departure_time=dep; j.server_id=ev.server;
                completed.push_back(j);
                pq.push({dep, 1, ev.server});
            }
        }
    }

    // Build result
    SimResult r;
    r.model_name  = name;
    r.num_servers = c;
    r.lambda      = lambda;
    r.mu          = mu_mean;
    r.rho         = lambda / (c * mu_mean);

    r.waiting_times.reserve(completed.size());
    r.sojourn_times.reserve(completed.size());
    for (auto& j : completed) {
        r.waiting_times.push_back(j.waiting_time());
        r.sojourn_times.push_back(j.sojourn_time());
    }

    auto mean_v = [](const std::vector<double>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    r.mean_waiting_time  = mean_v(r.waiting_times);
    r.mean_sojourn_time  = mean_v(r.sojourn_times);
    r.queue_lengths_over_time = q_lengths;
    r.time_points         = q_times;
    r.mean_queue_length   = mean_v(q_lengths);
    r.mean_system_length  = r.mean_queue_length + c * r.rho;
    r.throughput          = lambda;
    r.server_utilization  = r.rho;

    // M/M/c theoretical baseline
    r.theory_Wq = Theory::mmc_Wq(c, lambda, mu_mean);
    r.theory_W  = Theory::mmc_W (c, lambda, mu_mean);
    r.theory_Lq = Theory::mmc_Lq(c, lambda, mu_mean);
    r.theory_L  = Theory::mmc_L (c, lambda, mu_mean);

    return r;
}

int main() {
    const int N  = 150000;
    int       c  = 4;
    double    rho = 0.80;
    double    mu  = 3.0;
    double    lambda = rho * c * mu;

    std::cout << "=== General Service Distributions (c=" << c
              << ", ρ=" << rho << ") ===\n";

    std::ofstream summary("../data/exp4_summary.csv");
    write_summary_header(summary);

    // 1. M/M/c  (baseline)
    {
        RNG rng(1);
        auto dist = [&]() { return rng.exponential(mu); };
        auto r = simulate_general(c, lambda, dist, mu, N, 1, "M/M/c");
        write_summary_row(summary, r);
        write_jobs_csv(r, "../data/exp4_jobs_mmc.csv");
        std::cout << "  M/M/" << c << "  Wq=" << r.mean_waiting_time
                  << "  (theory=" << r.theory_Wq << ")\n";
    }

    // 2. M/D/c  (deterministic, Cs²=0)
    {
        double mean_svc = 1.0 / mu;
        auto dist = [mean_svc]() { return mean_svc; };
        auto r = simulate_general(c, lambda, dist, mu, N, 2, "M/D/c");
        write_summary_row(summary, r);
        write_jobs_csv(r, "../data/exp4_jobs_mdc.csv");
        std::cout << "  M/D/" << c << "  Wq=" << r.mean_waiting_time
                  << "  (Kingman≈" << r.theory_Wq * 0.5 << ")\n";
    }

    // 3. M/E2/c  (Erlang-2, Cs²=0.5)
    {
        RNG rng(3);
        auto dist = [&]() { return rng.erlang(2, mu); };
        auto r = simulate_general(c, lambda, dist, mu, N, 3, "M/E2/c");
        write_summary_row(summary, r);
        write_jobs_csv(r, "../data/exp4_jobs_me2c.csv");
        std::cout << "  M/E2/" << c << " Wq=" << r.mean_waiting_time
                  << "  (Kingman≈" << r.theory_Wq * 0.75 << ")\n";
    }

    // 4. M/E4/c  (Erlang-4, Cs²=0.75)
    {
        RNG rng(4);
        auto dist = [&]() { return rng.erlang(4, mu); };
        auto r = simulate_general(c, lambda, dist, mu, N, 4, "M/E4/c");
        write_summary_row(summary, r);
        write_jobs_csv(r, "../data/exp4_jobs_me4c.csv");
        std::cout << "  M/E4/" << c << " Wq=" << r.mean_waiting_time
                  << "  (Kingman≈" << r.theory_Wq * 0.875 << ")\n";
    }

    // 5. M/H2/c  (Hyper-exp, Cs²>1 → bursty service)
    {
        RNG rng(5);
        // H2: mix of fast (μ₁=6) and slow (μ₂=1) — mean ≈ 1/3  (=1/mu with mu=3)
        // p1=0.75, μ1=4μ, p2=0.25, μ2=μ/3  → E[S]=0.75/12 + 0.25/1 ≈ 1/3
        double mu1=4*mu, mu2=mu/3.0, p1=0.75;
        auto dist = [&]() { return rng.hyperexp(p1, mu1, mu2); };
        // Compute effective mean
        double eff_mu = 1.0 / (p1/mu1 + (1-p1)/mu2);
        auto r = simulate_general(c, lambda, dist, eff_mu, N, 5, "M/H2/c");
        write_summary_row(summary, r);
        write_jobs_csv(r, "../data/exp4_jobs_mh2c.csv");
        std::cout << "  M/H2/" << c << " Wq=" << r.mean_waiting_time
                  << "  (heavy variance → longer waits)\n";
    }

    std::cout << "  → data/exp4_summary.csv\n";
    return 0;
}
