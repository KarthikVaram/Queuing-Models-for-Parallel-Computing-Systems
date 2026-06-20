#pragma once
#include <vector>
#include <queue>
#include <random>
#include <functional>
#include <string>
#include <numeric>
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Data Structures
// ─────────────────────────────────────────────────────────────────────────────

struct Job {
    int    id;
    double arrival_time;
    double service_start;
    double departure_time;
    int    server_id;       // which core served this job (-1 = not yet assigned)

    double waiting_time()   const { return service_start  - arrival_time;  }
    double sojourn_time()   const { return departure_time - arrival_time;   }
    double service_time()   const { return departure_time - service_start;  }
};

struct SimResult {
    std::string model_name;
    int         num_servers;
    double      lambda;          // arrival rate
    double      mu;              // per-server service rate
    double      rho;             // utilization = lambda / (num_servers * mu)

    // Empirical metrics
    double mean_waiting_time;
    double mean_sojourn_time;
    double mean_queue_length;
    double mean_system_length;
    double throughput;
    double server_utilization;

    // Theoretical metrics (M/M/c formulas)
    double theory_Wq;            // E[Wq]
    double theory_W;             // E[W]
    double theory_Lq;            // E[Lq]
    double theory_L;             // E[L]

    std::vector<double> waiting_times;
    std::vector<double> sojourn_times;
    std::vector<double> queue_lengths_over_time;
    std::vector<double> time_points;
};

// ─────────────────────────────────────────────────────────────────────────────
// RNG Utilities
// ─────────────────────────────────────────────────────────────────────────────

class RNG {
public:
    explicit RNG(unsigned seed = 42) : gen_(seed) {}

    double exponential(double rate) {
        std::exponential_distribution<double> d(rate);
        return d(gen_);
    }

    double uniform() {
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return d(gen_);
    }

    // Erlang-k distribution: sum of k exponentials (for non-Markovian service)
    double erlang(int k, double mu) {
        double s = 0.0;
        for (int i = 0; i < k; ++i) s += exponential(k * mu);
        return s;
    }

    // Hyper-exponential: mixture of two exponentials
    double hyperexp(double p1, double mu1, double mu2) {
        return (uniform() < p1) ? exponential(mu1) : exponential(mu2);
    }

private:
    std::mt19937_64 gen_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Theoretical M/M/c Formulas
// ─────────────────────────────────────────────────────────────────────────────

namespace Theory {

    // Erlang-C formula: P(waiting) for M/M/c
    inline double erlang_C(int c, double rho_total) {
        // rho_total = lambda / mu  (total load, not per-server)
        double a = rho_total;          // offered load
        double rho = a / c;            // per-server utilization
        if (rho >= 1.0) return 1.0;   // unstable

        // Numerator: a^c / c! * 1/(1-rho)
        double num = 1.0;
        for (int i = 1; i <= c; ++i) num *= a / i;
        num /= (1.0 - rho);

        // Denominator: sum_{k=0}^{c-1} a^k/k!  +  above
        double denom = 0.0;
        double term  = 1.0;
        for (int k = 0; k < c; ++k) {
            denom += term;
            term  *= a / (k + 1);
        }
        denom += num;
        return num / denom;
    }

    inline double mmc_Wq(int c, double lambda, double mu) {
        double rho = lambda / (c * mu);
        if (rho >= 1.0) return 1e9;
        double C = erlang_C(c, lambda / mu);
        return C / (c * mu * (1.0 - rho) * lambda);
    }

    inline double mmc_W(int c, double lambda, double mu) {
        return mmc_Wq(c, lambda, mu) + 1.0 / mu;
    }

    inline double mmc_Lq(int c, double lambda, double mu) {
        return lambda * mmc_Wq(c, lambda, mu);
    }

    inline double mmc_L(int c, double lambda, double mu) {
        return lambda * mmc_W(c, lambda, mu);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Event-Driven M/M/c Simulator
// ─────────────────────────────────────────────────────────────────────────────

class MMcSimulator {
public:
    MMcSimulator(int c, double lambda, double mu,
                 int n_jobs = 200000, unsigned seed = 42)
        : c_(c), lambda_(lambda), mu_(mu), n_jobs_(n_jobs), rng_(seed) {}

    SimResult run(const std::string& model_name = "M/M/c") {
        // Event types: ARRIVAL=0, DEPARTURE=1
        struct Event {
            double time;
            int    type;   // 0=arrival, 1=departure
            int    server; // relevant for departure
            bool operator>(const Event& o) const { return time > o.time; }
        };

        std::priority_queue<Event, std::vector<Event>, std::greater<Event>> pq;

        std::vector<double> server_free_at(c_, 0.0); // when each server becomes free
        std::queue<double>  waiting;                  // arrival times of waiting jobs
        std::vector<Job>    completed;
        completed.reserve(n_jobs_);

        // Timeline sampling for queue length
        std::vector<double> q_times, q_lengths;
        double sample_interval = 1.0 / lambda_ * 0.5;
        double next_sample     = 0.0;
        int    queue_len_snap  = 0;
        int    jobs_in_system  = 0;

        double t   = 0.0;
        int    id  = 0;
        int    departed = 0;

        // Schedule first arrival
        pq.push({rng_.exponential(lambda_), 0, -1});

        while (departed < n_jobs_) {
            if (pq.empty()) break;
            Event ev = pq.top(); pq.pop();
            t = ev.time;

            // Sample queue length
            while (next_sample <= t) {
                q_times.push_back(next_sample);
                q_lengths.push_back(static_cast<double>(queue_len_snap));
                next_sample += sample_interval;
            }

            if (ev.type == 0) {
                // ── ARRIVAL ──
                ++id;
                ++jobs_in_system;
                queue_len_snap = std::max(0, jobs_in_system - c_);

                // Find a free server
                int free_srv = -1;
                for (int i = 0; i < c_; ++i) {
                    if (server_free_at[i] <= t) { free_srv = i; break; }
                }

                double svc = rng_.exponential(mu_);

                if (free_srv >= 0) {
                    // Serve immediately
                    double start = t;
                    double depart = start + svc;
                    server_free_at[free_srv] = depart;
                    Job j; j.id=id; j.arrival_time=t; j.service_start=start;
                    j.departure_time=depart; j.server_id=free_srv;
                    completed.push_back(j);
                    pq.push({depart, 1, free_srv});
                } else {
                    // Queue the arrival time + service duration
                    waiting.push(t);
                    // store service time alongside – pack into a second queue
                    svc_times_.push(svc);
                }

                if (id < n_jobs_ * 2)  // keep scheduling arrivals
                    pq.push({t + rng_.exponential(lambda_), 0, -1});

            } else {
                // ── DEPARTURE ──
                ++departed;
                --jobs_in_system;
                queue_len_snap = std::max(0, jobs_in_system - c_);

                if (!waiting.empty()) {
                    double arr_t  = waiting.front(); waiting.pop();
                    double svc    = svc_times_.front(); svc_times_.pop();
                    double start  = t;
                    double depart = start + svc;
                    server_free_at[ev.server] = depart;
                    Job j; j.id=++id; j.arrival_time=arr_t; j.service_start=start;
                    j.departure_time=depart; j.server_id=ev.server;
                    completed.push_back(j);
                    pq.push({depart, 1, ev.server});
                }
            }
        }

        return build_result(model_name, completed, q_times, q_lengths);
    }

private:
    int    c_;
    double lambda_, mu_;
    int    n_jobs_;
    RNG    rng_;
    std::queue<double> svc_times_;

    SimResult build_result(const std::string& name,
                           const std::vector<Job>& jobs,
                           const std::vector<double>& qt,
                           const std::vector<double>& ql) {
        SimResult r;
        r.model_name  = name;
        r.num_servers = c_;
        r.lambda      = lambda_;
        r.mu          = mu_;
        r.rho         = lambda_ / (c_ * mu_);

        r.waiting_times.reserve(jobs.size());
        r.sojourn_times.reserve(jobs.size());
        for (auto& j : jobs) {
            r.waiting_times.push_back(j.waiting_time());
            r.sojourn_times.push_back(j.sojourn_time());
        }

        auto mean = [](const std::vector<double>& v) {
            return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        };

        r.mean_waiting_time  = mean(r.waiting_times);
        r.mean_sojourn_time  = mean(r.sojourn_times);
        r.queue_lengths_over_time = ql;
        r.time_points         = qt;
        r.mean_queue_length   = mean(ql);
        r.mean_system_length  = r.mean_queue_length + c_ * r.rho;
        r.throughput          = lambda_;
        r.server_utilization  = r.rho;

        // Theoretical
        r.theory_Wq = Theory::mmc_Wq(c_, lambda_, mu_);
        r.theory_W  = Theory::mmc_W (c_, lambda_, mu_);
        r.theory_Lq = Theory::mmc_Lq(c_, lambda_, mu_);
        r.theory_L  = Theory::mmc_L (c_, lambda_, mu_);

        return r;
    }
};
