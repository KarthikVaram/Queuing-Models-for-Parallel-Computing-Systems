#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include "queue_models.h"

// Write per-job metrics to CSV
inline void write_jobs_csv(const SimResult& r, const std::string& path) {
    std::ofstream f(path);
    f << std::fixed << std::setprecision(6);
    f << "job_id,waiting_time,sojourn_time\n";
    for (size_t i = 0; i < r.waiting_times.size(); ++i)
        f << i+1 << "," << r.waiting_times[i] << "," << r.sojourn_times[i] << "\n";
}

// Write queue-length time series to CSV
inline void write_queue_ts_csv(const SimResult& r, const std::string& path) {
    std::ofstream f(path);
    f << std::fixed << std::setprecision(4);
    f << "time,queue_length\n";
    for (size_t i = 0; i < r.time_points.size(); ++i)
        f << r.time_points[i] << "," << r.queue_lengths_over_time[i] << "\n";
}

// Write summary metrics to CSV (one row per experiment)
inline void write_summary_row(std::ofstream& f, const SimResult& r) {
    f << std::fixed << std::setprecision(6);
    f << r.model_name << ","
      << r.num_servers << ","
      << r.lambda << ","
      << r.mu << ","
      << r.rho << ","
      << r.mean_waiting_time << ","
      << r.mean_sojourn_time << ","
      << r.mean_queue_length << ","
      << r.mean_system_length << ","
      << r.throughput << ","
      << r.server_utilization << ","
      << r.theory_Wq << ","
      << r.theory_W  << ","
      << r.theory_Lq << ","
      << r.theory_L  << "\n";
}

inline void write_summary_header(std::ofstream& f) {
    f << "model,num_servers,lambda,mu,rho,"
         "sim_Wq,sim_W,sim_Lq,sim_L,throughput,utilization,"
         "theory_Wq,theory_W,theory_Lq,theory_L\n";
}
