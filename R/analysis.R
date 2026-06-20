# =============================================================================
# Queueing Models for Parallel Computing Systems — R Analysis
# =============================================================================
library(ggplot2); library(dplyr); library(tidyr)
library(scales);  library(gridExtra); library(grid)

theme_queue <- function(base_size = 13) {
  theme_minimal(base_size = base_size) +
    theme(
      plot.title    = element_text(face="bold", size=base_size+2, hjust=0),
      plot.subtitle = element_text(color="grey40", size=base_size-1, hjust=0),
      plot.caption  = element_text(color="grey50", size=9, hjust=1),
      axis.title    = element_text(size=base_size),
      axis.text     = element_text(size=base_size-2),
      legend.title  = element_text(size=base_size-1, face="bold"),
      legend.text   = element_text(size=base_size-2),
      panel.grid.minor = element_blank(),
      panel.grid.major = element_line(color="grey90"),
      strip.text    = element_text(face="bold", size=base_size-1),
      plot.background = element_rect(fill="white", color=NA)
    )
}

PAL <- c("#2C7BB6","#D7191C","#1A9641","#F4A11D","#984EA3","#FF7F00","#4B4B4B","#A65628")

save_p <- function(p, f, w=9, h=6) {
  ggsave(file.path("../plots", f), p, width=w, height=h, dpi=180, bg="white")
  cat("  Saved:", f, "\n")
}

cat("=== Queueing Models R Analysis ===\n")

# ── Erlang-C in R ─────────────────────────────────────────────────────────────
erlang_C <- function(c, a) {
  rho <- a / c; if (rho >= 1) return(1)
  num <- prod(a / (1:c)) / (1 - rho)
  denom <- sum(sapply(0:(c-1), function(k) a^k / factorial(k))) + num
  num / denom
}
mmc_Wq <- function(c, lam, mu) {
  a <- lam/mu; rho <- lam/(c*mu); if(rho>=1) return(Inf)
  erlang_C(c,a) / (c*mu*(1-rho)*lam)
}

# =============================================================================
# PLOT 1: Servers vs Wq — simulation + theory overlay
# =============================================================================
cat("\n[1] Server Scaling\n")
exp1 <- read.csv("../data/exp1_summary.csv")

theory1 <- data.frame(c = seq(5,20,by=0.5)) %>%
  mutate(lam=8, mu=2, rho=lam/(c*mu)) %>%
  filter(rho < 1) %>%
  rowwise() %>% mutate(Wq = mmc_Wq(c, lam, mu)) %>% ungroup()

p1 <- ggplot() +
  geom_line(data=theory1, aes(x=c, y=Wq, linetype="Erlang-C Theory"),
            color="grey40", linewidth=0.9) +
  geom_point(data=exp1, aes(x=num_servers, y=sim_Wq, color="Simulation"),
             size=4, shape=16) +
  geom_point(data=exp1, aes(x=num_servers, y=theory_Wq, color="Theory (at sim points)"),
             size=4, shape=1, stroke=1.4) +
  scale_color_manual(values=c("Simulation"=PAL[1],"Theory (at sim points)"=PAL[2]), name=NULL) +
  scale_linetype_manual(values=c("Erlang-C Theory"="dashed"), name=NULL) +
  scale_y_log10() +
  labs(title="M/M/c: Mean Waiting Time vs Number of Servers",
       subtitle="λ=8, μ=2  |  log-scale y  |  N=150,000 jobs per run",
       x="Number of servers (c)", y="Wq — Mean waiting time (log scale)",
       caption="Open circles = Erlang-C theory, filled = simulation") +
  theme_queue() + theme(legend.position="top")
save_p(p1, "01_server_scaling.png")

# =============================================================================
# PLOT 2: Utilisation saturation
# =============================================================================
cat("[2] Utilisation saturation\n")
exp2 <- read.csv("../data/exp2_summary.csv")

p2 <- ggplot(exp2, aes(x=rho)) +
  geom_line(aes(y=theory_Wq, color="Erlang-C Theory"), linetype="dashed", linewidth=1) +
  geom_line(aes(y=sim_Wq, color="Simulation"), linewidth=1.2) +
  geom_point(aes(y=sim_Wq, color="Simulation"), size=3) +
  geom_vline(xintercept=0.9, linetype="dotted", color="grey50") +
  annotate("text", x=0.915, y=max(exp2$sim_Wq)*0.55,
           label="ρ=0.9\nknee", size=3.5, hjust=0, color="grey40") +
  scale_color_manual(values=c("Simulation"=PAL[1],"Erlang-C Theory"=PAL[2]), name=NULL) +
  scale_x_continuous(labels=percent, breaks=seq(0,1,0.1)) +
  labs(title="Saturation: Waiting Time Explodes as ρ → 1",
       subtitle="M/M/4  |  c=4, μ=3 — non-linear blow-up near saturation",
       x="Server utilization ρ = λ/(cμ)", y="Mean waiting time Wq",
       caption="N=150,000 jobs per data point") +
  theme_queue() + theme(legend.position="top")
save_p(p2, "02_utilization_blowup.png")

# =============================================================================
# PLOT 3: Scalability
# =============================================================================
cat("[3] Scalability\n")
exp3 <- read.csv("../data/exp3_summary.csv")

p3a <- ggplot(exp3, aes(x=num_servers, y=throughput)) +
  geom_abline(slope=exp3$throughput[1]/exp3$num_servers[1], intercept=0,
              linetype="dashed", color="grey50", linewidth=0.8) +
  geom_line(color=PAL[1], linewidth=1.2) +
  geom_point(color=PAL[1], size=3.5) +
  annotate("text", x=20, y=exp3$throughput[1]/exp3$num_servers[1]*20*0.92,
           label="Perfect\nlinear", size=3.5, color="grey50") +
  scale_x_continuous(breaks=c(1,2,4,8,16,32)) +
  labs(title="Throughput Scales Linearly", subtitle="ρ = 0.75 fixed per server",
       x="Cores (c)", y="Throughput (jobs/s)") +
  theme_queue()

p3b <- ggplot(exp3, aes(x=num_servers, y=sim_Wq)) +
  geom_line(color=PAL[3], linewidth=1.2) +
  geom_point(color=PAL[3], size=3.5) +
  scale_x_continuous(breaks=c(1,2,4,8,16,32)) +
  labs(title="Waiting Time Drops Sharply", subtitle="Same ρ, more cores → shorter queues",
       x="Cores (c)", y="Mean waiting time Wq") +
  theme_queue()

p3 <- grid.arrange(p3a, p3b, ncol=2,
  top=textGrob("Ideal Scalability: Constant ρ = 0.75, Cores Doubled Repeatedly",
               gp=gpar(fontface="bold", fontsize=14)))
ggsave("../plots/03_scalability.png", p3, width=12, height=5.5, dpi=180, bg="white")
cat("  Saved: 03_scalability.png\n")

# =============================================================================
# PLOT 4: Service distribution bars + Kingman
# =============================================================================
cat("[4] Service distributions\n")
exp4 <- read.csv("../data/exp4_summary.csv")
cv2_map <- c("M/M/c"=1.0,"M/D/c"=0.0,"M/E2/c"=0.5,"M/E4/c"=0.25,"M/H2/c"=2.5)
exp4 <- exp4 %>%
  mutate(Cs2 = cv2_map[model],
         kingman = theory_Wq * (1 + Cs2) / 2,
         model   = factor(model, levels=c("M/D/c","M/E4/c","M/E2/c","M/M/c","M/H2/c")))

p4 <- ggplot(exp4, aes(x=model, y=sim_Wq, fill=model)) +
  geom_col(width=0.6, alpha=0.85) +
  geom_point(aes(y=kingman), shape=18, size=5, color="black") +
  scale_fill_manual(values=setNames(PAL[1:5], levels(exp4$model))) +
  annotate("text", x=5.5, y=max(exp4$sim_Wq)*0.9,
           label="◆ = Kingman\n  approximation", size=3.5, hjust=0) +
  labs(title="Service-Time Variance Drives Waiting Time",
       subtitle="c=4, ρ=0.80 | Same mean rate, different Cs² (squared coeff. of variation)",
       x="Queue model", y="Mean waiting time Wq",
       caption="Allen–Cunneen: Wq(G) ≈ Wq(M/M/c) × (1+Cs²)/2") +
  theme_queue() + theme(legend.position="none") +
  coord_cartesian(xlim=c(0.5, 5.9))
save_p(p4, "04_service_distributions.png", w=9, h=5.5)

# =============================================================================
# PLOT 4b: Waiting time violin
# =============================================================================
cat("[4b] Waiting time violins\n")
files4 <- list("M/M/c"="../data/exp4_jobs_mmc.csv",
               "M/D/c"="../data/exp4_jobs_mdc.csv",
               "M/E2/c"="../data/exp4_jobs_me2c.csv",
               "M/H2/c"="../data/exp4_jobs_mh2c.csv")
jobs4 <- bind_rows(lapply(names(files4), function(nm) {
  df <- read.csv(files4[[nm]]); df$model <- nm; df
})) %>%
  filter(waiting_time > 1e-6) %>%
  mutate(model=factor(model, levels=c("M/D/c","M/E2/c","M/M/c","M/H2/c")))

p4b <- ggplot(jobs4, aes(x=model, y=waiting_time, fill=model)) +
  geom_violin(trim=TRUE, alpha=0.6, scale="width") +
  geom_boxplot(width=0.1, outlier.shape=NA, alpha=0.9, color="white") +
  scale_y_log10(labels=label_number(accuracy=0.001)) +
  scale_fill_manual(values=setNames(PAL[c(3,4,1,2)], levels(jobs4$model))) +
  labs(title="Waiting-Time Distributions by Service Model",
       subtitle="Violin + boxplot  |  log-scale y  |  ρ=0.80, c=4",
       x="Model", y="Waiting time (log scale)") +
  theme_queue() + theme(legend.position="none")
save_p(p4b, "04b_waiting_distributions.png", w=8, h=5.5)

# =============================================================================
# PLOT 5: Load balancing
# =============================================================================
cat("[5] Load balancing\n")
exp5 <- read.csv("../data/exp5_summary.csv")
plvls  <- c("Random","RoundRobin","Po2C","JSQ")
plabls <- c("Random","Round Robin","Power-of-2","JSQ (optimal)")
exp5$policy <- factor(exp5$policy, levels=plvls, labels=plabls)

theory5 <- exp5 %>% distinct(rho, .keep_all=TRUE)

p5 <- ggplot(exp5, aes(x=rho, y=sim_Wq, color=policy, group=policy)) +
  geom_line(linewidth=1.2) + geom_point(size=3) +
  geom_line(data=theory5, aes(y=theory_Wq, group=1),
            linetype="dashed", color="black", linewidth=0.7) +
  annotate("text", x=0.92, y=theory5$theory_Wq[nrow(theory5)]*1.5,
           label="M/M/c bound\n(shared queue)", size=3, color="black") +
  scale_color_manual(values=setNames(PAL[c(2,4,3,1)], plabls), name="Dispatch Policy") +
  scale_x_continuous(labels=percent, breaks=seq(0.5,1,0.1)) +
  labs(title="Load Balancing Policies: Wq vs Utilization  (c=8 servers)",
       subtitle="JSQ is optimal; Power-of-2 choices is near-optimal at low overhead",
       x="Utilization ρ", y="Mean waiting time Wq",
       caption="Dashed = Erlang-C lower bound (perfect shared queue)") +
  theme_queue() + theme(legend.position="right")
save_p(p5, "05_load_balancing.png", w=10, h=6)

# =============================================================================
# PLOT 6: Wq heatmap over (c, rho) parameter space
# =============================================================================
cat("[6] Parameter-space heatmap\n")
heat <- expand.grid(c=1:16, rho=seq(0.1,0.95,by=0.05)) %>%
  mutate(mu=2, lam=rho*c*mu) %>%
  rowwise() %>%
  mutate(Wq = ifelse(rho >= 1, NA, mmc_Wq(c, lam, mu))) %>%
  ungroup()

p6 <- ggplot(heat, aes(x=factor(c), y=rho, fill=pmin(Wq,3))) +
  geom_tile() +
  scale_fill_gradientn(colors=c("#2166AC","#92C5DE","#F7F700","#D7191C"),
                       na.value="grey70",
                       name="Wq\n(capped 3)",
                       labels=label_number(accuracy=0.1)) +
  scale_y_continuous(labels=percent, breaks=seq(0.1,0.95,0.1)) +
  labs(title="Erlang-C Mean Waiting Time: Full (c, ρ) Parameter Space",
       subtitle="μ=2 per server  |  Blue = short wait, red = long wait",
       x="Number of servers (c)", y="Utilization ρ") +
  theme_queue()
save_p(p6, "06_wq_heatmap.png", w=10, h=6)

# =============================================================================
# PLOT 7: Queue-length time series (exp2 rho=50,80,93)
# =============================================================================
cat("[7] Queue length time series\n")
# Use exp2 per-job files to compute running averages of wait
files_rho <- list(
  "ρ = 0.50" = "../data/exp2_jobs_rho50.csv",
  "ρ = 0.80" = "../data/exp2_jobs_rho80.csv",
  "ρ = 0.93" = "../data/exp2_jobs_rho93.csv"
)
jobs_rho <- bind_rows(lapply(names(files_rho), function(nm) {
  df <- read.csv(files_rho[[nm]]); df$label <- nm; df
})) %>%
  mutate(label = factor(label, levels=names(files_rho)))

# Running cumulative mean of waiting time
jobs_rho <- jobs_rho %>%
  group_by(label) %>%
  mutate(cum_wq = cumsum(waiting_time) / seq_along(waiting_time)) %>%
  ungroup() %>%
  filter(job_id <= 50000)   # first 50k jobs for clarity

p7 <- ggplot(jobs_rho, aes(x=job_id, y=cum_wq, color=label)) +
  geom_line(linewidth=0.9, alpha=0.85) +
  facet_wrap(~label, ncol=1, scales="free_y") +
  scale_color_manual(values=setNames(PAL[c(1,3,2)], levels(jobs_rho$label))) +
  scale_x_continuous(labels=label_comma()) +
  labs(title="Convergence of Cumulative Mean Waiting Time  (M/M/4)",
       subtitle="Cumulative average stabilises as more jobs complete — verifies stationarity",
       x="Job number", y="Cumulative mean Wq") +
  theme_queue() + theme(legend.position="none")
save_p(p7, "07_convergence.png", w=10, h=7)

# =============================================================================
# PLOT 8: Policy Wq at ρ = 0.80 — bar chart
# =============================================================================
cat("[8] Policy comparison bar chart\n")
exp5_80 <- exp5 %>% filter(abs(rho - 0.80) < 0.01) %>%
  mutate(policy=factor(policy, levels=rev(plabls)))

p8 <- ggplot(exp5_80, aes(x=policy, y=sim_Wq, fill=policy)) +
  geom_col(width=0.6, alpha=0.85) +
  geom_hline(aes(yintercept=theory_Wq[1]), linetype="dashed", color="black") +
  annotate("text", x=4.6, y=exp5_80$theory_Wq[1]*1.06,
           label="M/M/c bound", size=3.5, hjust=1, color="black") +
  coord_flip() +
  scale_fill_manual(values=setNames(PAL[c(1,3,4,2)], levels(exp5_80$policy))) +
  labs(title="Load Balancing: Mean Waiting Time at ρ = 0.80  (c=8)",
       subtitle="JSQ minimises Wq; Power-of-2 offers excellent cost-accuracy tradeoff",
       x=NULL, y="Mean waiting time Wq",
       caption="Dashed = theoretical optimum (global shared M/M/c queue)") +
  theme_queue() + theme(legend.position="none")
save_p(p8, "08_policy_bar.png", w=8, h=5)

cat("\n=== All", length(list.files("../plots")), "plots saved to plots/ ===\n")
