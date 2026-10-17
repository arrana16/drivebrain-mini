#pragma once
#include <linux/gpio.h>

#include <DriverBus.hpp>
#include <atomic>
#include <cstdint>
#include <thread>

struct PulseEvent {
    std::uint64_t timestamp_ns;
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

    FlowmeterComms(const FlowmeterComms&) = delete;
    FlowmeterComms& operator=(const FlowmeterComms&) = delete;

private:
    void read_loop();

    unsigned int gpio_pin_;
    core::ThreadSafeDeque<PulseEvent>& pulse_queue_;

    std::atomic<bool> running_{false};
    std::thread reader_thread_;
};
