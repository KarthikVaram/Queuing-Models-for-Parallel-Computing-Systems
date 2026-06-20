/*  sim_loadbalance.cpp  ─  Load Balancing Policies for Parallel Systems
 *
 *  Compares three dispatch policies under the same M/M arrival stream:
 *    1. Round-Robin  (JSQ approximation without observation)
 *    2. Join-Shortest-Queue (JSQ) — optimal without migration cost
 *    3. Random  (baseline, worst)
 *    4. Power-of-Two-Choices (Po2C) — practical JSQ approximation
 *
 *  Compile:
 *    g++ -O2 -std=c++17 -I../include sim_loadbalance.cpp -o sim_loadbalance
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <deque>
#include <functional>
#include <string>
#include <numeric>
#include <algorithm>
#include <limits>
#include "queue_models.h"
#include "csv_writer.h"

enum Policy { RANDOM, ROUND_ROBIN, JSQ, POWER_OF_TWO };

SimResult simulate_lb(int c, double lambda, double mu,
                      Policy policy, int n_jobs, unsigned seed,
                      const std::string& name) {
    RNG rng(seed);

    // Each server has its own FIFO queue
    struct Server {
        double        busy_until = 0.0;
        std::deque<double> arrivals;  // arrival times of queued jobs
        std::deque<double> svctimes;

        int queue_len() const { return (int)arrivals.size(); }
        bool is_free(double t) const { return busy_until <= t; }
    };

    std::vector<Server> servers(c);
    std::vector<Job>    completed;
    completed.reserve(n_jobs);

    // Event: type 0=arrival, type 1=departure
    struct Event {
        double time; int type; int server;
        bool operator>(const Event& o) const { return time > o.time; }
    };
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> pq;

    std::vector<double> q_times, q_lengths;
    double sample_dt = 1.0 / lambda * 0.5;
    double next_s    = 0.0;

    int rr_idx  = 0;   // round-robin counter
    int job_id  = 0;
    int departed= 0;

    pq.push({rng.exponential(lambda), 0, -1});

    auto total_queue = [&]() {
        int q = 0;
        for (auto& s : servers) q += s.queue_len();
        return q;
    };

    while (departed < n_jobs) {
        if (pq.empty()) break;
        auto ev = pq.top(); pq.pop();
        double t = ev.time;

        while (next_s <= t) {
            q_times.push_back(next_s);
            q_lengths.push_back(static_cast<double>(total_queue()));
            next_s += sample_dt;
        }

        if (ev.type == 0) {
            // ── ARRIVAL: choose server by policy ──
            ++job_id;
            double svc = rng.exponential(mu);
            int chosen = 0;

            switch (policy) {
              case RANDOM:
                chosen = (int)(rng.uniform() * c) % c;
                break;

              case ROUND_ROBIN:
                chosen = rr_idx % c;
                ++rr_idx;
                break;

              case JSQ: {
                // Find server with shortest queue; break ties by soonest free
                int best_q = std::numeric_limits<int>::max();
                double best_t = std::numeric_limits<double>::max();
                for (int i = 0; i < c; ++i) {
                    int q = servers[i].queue_len() + (servers[i].is_free(t) ? 0 : 1);
                    if (q < best_q || (q == best_q && servers[i].busy_until < best_t)) {
                        best_q = q; best_t = servers[i].busy_until; chosen = i;
                    }
                }
                break;
              }

              case POWER_OF_TWO: {
                // Sample 2 random servers, pick shorter queue
                int a = (int)(rng.uniform() * c) % c;
                int b = (int)(rng.uniform() * c) % c;
                if (b == a) b = (a + 1) % c;
                int qa = servers[a].queue_len() + (servers[a].is_free(t)?0:1);
                int qb = servers[b].queue_len() + (servers[b].is_free(t)?0:1);
                chosen = (qa <= qb) ? a : b;
                break;
              }
            }

            Server& srv = servers[chosen];
            if (srv.is_free(t)) {
                // Serve immediately
                double dep = t + svc;
                srv.busy_until = dep;
                Job j; j.id=job_id; j.arrival_time=t;
                j.service_start=t; j.departure_time=dep; j.server_id=chosen;
                completed.push_back(j);
                pq.push({dep, 1, chosen});
            } else {
                srv.arrivals.push_back(t);
                srv.svctimes.push_back(svc);
            }

            if (job_id < n_jobs * 2)
                pq.push({t + rng.exponential(lambda), 0, -1});

        } else {
            // ── DEPARTURE ──
            ++departed;
            Server& srv = servers[ev.server];
            if (!srv.arrivals.empty()) {
                double arr = srv.arrivals.front(); srv.arrivals.pop_front();
                double svc = srv.svctimes.front(); srv.svctimes.pop_front();
                double dep = t + svc;
                srv.busy_until = dep;
                Job j; j.id=++job_id; j.arrival_time=arr;
                j.service_start=t; j.departure_time=dep; j.server_id=ev.server;
                completed.push_back(j);
                pq.push({dep, 1, ev.server});
            } else {
                srv.busy_until = t;
            }
        }
    }

    SimResult r;
    r.model_name  = name;
    r.num_servers = c;
    r.lambda      = lambda;
    r.mu          = mu;
    r.rho         = lambda / (c * mu);

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
    r.throughput          = lambda;
    r.server_utilization  = r.rho;

    r.theory_Wq = Theory::mmc_Wq(c, lambda, mu);
    r.theory_W  = Theory::mmc_W (c, lambda, mu);
    r.theory_Lq = Theory::mmc_Lq(c, lambda, mu);
    r.theory_L  = Theory::mmc_L (c, lambda, mu);

    return r;
}

int main() {
    const int N  = 100000;
    int c        = 8;
    double mu    = 2.0;

    std::vector<double> rhos = {0.50, 0.70, 0.80, 0.90, 0.95};

    std::ofstream summary("../data/exp5_summary.csv");
    summary << "policy,num_servers,lambda,mu,rho,sim_Wq,sim_W,theory_Wq\n";

    std::cout << "=== Experiment 5: Load Balancing Policies (c=" << c << ") ===\n";

    for (double rho : rhos) {
        double lambda = rho * c * mu;
        std::cout << "  ρ=" << rho << "\n";

        struct Run { Policy p; std::string name; };
        std::vector<Run> runs = {
            {RANDOM,       "Random"},
            {ROUND_ROBIN,  "RoundRobin"},
            {POWER_OF_TWO, "Po2C"},
            {JSQ,          "JSQ"}
        };

        for (auto& run : runs) {
            auto r = simulate_lb(c, lambda, mu, run.p, N, 42, run.name);
            summary << std::fixed << std::setprecision(6)
                    << run.name << "," << c << "," << lambda << ","
                    << mu << "," << rho << ","
                    << r.mean_waiting_time << "," << r.mean_sojourn_time << ","
                    << r.theory_Wq << "\n";
            std::cout << "    " << run.name
                      << "  Wq=" << r.mean_waiting_time << "\n";

            if (rho == 0.80)
                write_jobs_csv(r, "../data/exp5_jobs_" + run.name + ".csv");
        }
    }

    std::cout << "  → data/exp5_summary.csv\n";
    return 0;
}
