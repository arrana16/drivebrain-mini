#pragma once
#include <DriverBus.hpp>
#include <atomic>
#include <cstdint>
#include <thread>
#include <optional>

struct PulseEvent {
    std::uint64_t timestamp_ns;
};

struct FlowmeterState {
    std::uint64_t version;
    std::uint64_t total_pulses;
    std::uint64_t first_pulse_timestamp_ns;
    std::uint64_t last_pulse_timestamp_ns;
};

class FlowmeterComms {
public:
    FlowmeterComms(
        unsigned int gpio_pin,
        core::ThreadSafeDeque<PulseEvent>& pulse_queue
    );

    ~FlowmeterComms();

    void start();
    void stop();
    std::optional<FlowmeterState> wait_for_update(std::uint64_t last_seen_version);

    FlowmeterComms(const FlowmeterComms&) = delete;
    FlowmeterComms& operator=(const FlowmeterComms&) = delete;

private:
    void read_loop();
    void state_loop();

    unsigned int gpio_pin_;
    core::ThreadSafeDeque<PulseEvent>& pulse_queue_;
    std::atomic<bool> running_{false};
    std::thread reader_thread_;

    FlowmeterState state_{};
    std::mutex state_mutex_;
    std::thread state_thread_;
    std::condition_variable state_cv;
};
