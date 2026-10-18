#include <FoxgloveServer.hpp>
#include <MCAPLogger.hpp>
#include <drivebrain_msgs.pb.h>
#include <FlowmeterComms.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <optional>
#include <thread>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>

std::atomic<bool> running{true};

void sig_handler(int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
      spdlog::warn("Interrupted, stopping mini-drivebrain");
      running = false;
    }
}

int main() {
  std::signal(SIGINT, sig_handler);
  std::signal(SIGTERM, sig_handler);

  core::MCAPLogger::create(mcap::McapWriterOptions(""));
  core::FoxgloveServer::create();

  core::MCAPLogger::instance().open_new_mcap();
  core::MCAPLogger::instance().init_logging();



  spdlog::info("Constructed logging singletons");

  auto loop_time = 0.004;
  std::chrono::microseconds loop_time_ms((int) (loop_time * 1000000.0f));
  auto next_tick = std::chrono::steady_clock::now();

  core::ThreadSafeDeque<PulseEvent> pulse_queue;
  unsigned int gpio_offset = 1;
  FlowmeterComms flowmeter(gpio_offset, pulse_queue);
  flowmeter.start();

  constexpr double pulses_per_liter = 450.0;
  std::uint64_t last_seen_version = 0;
  std::optional<FlowmeterState> previous;
  while(running) {

    next_tick += loop_time_ms;
    auto update = flowmeter.wait_for_update(last_seen_version);
    if (!update) {
        break;
    }
    const FlowmeterState curr = *update;
    last_seen_version = curr.version;

    // A skipped snapshot would leave pulses uncounted in this interval.
    if (previous && curr.version == previous->version + 1 &&
        curr.total_pulses > 0 &&
        curr.last_pulse_timestamp_ns > previous->last_pulse_timestamp_ns) {
      const double elapsed_seconds =
          (curr.last_pulse_timestamp_ns - previous->last_pulse_timestamp_ns) / 1e9;

      // Each pulse in this batch completes one interval since the previous pulse.
      const double flow_rate =
          curr.total_pulses * 60.0 / (pulses_per_liter * elapsed_seconds);

      auto message = std::make_shared<drivebrain_msgs::FlowmeterReading>();

      message->set_timestamp_ns(curr.last_pulse_timestamp_ns);
      message->set_flow_rate_liters_per_minute(flow_rate);

      core::FoxgloveServer::instance().send_live_telem_msg(message);
      core::MCAPLogger::instance().log_msg(message);
    }
    previous = curr;
    auto now = std::chrono::steady_clock::now();
    if(now > next_tick) {
      spdlog::warn("Loop overrun by {}", now-next_tick);
      next_tick = now;
    }

    std::this_thread::sleep_until(next_tick);
  }

  flowmeter.stop();
  core::MCAPLogger::instance().destroy();
  core::FoxgloveServer::instance().destroy();
  spdlog::info("Logging singletons safely destroyed");

  return 0;
}
