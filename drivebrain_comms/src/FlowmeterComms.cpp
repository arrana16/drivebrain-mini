#include <FlowmeterComms.hpp>

FlowmeterComms::FlowmeterComms(unsigned int gpio_pin,core::ThreadSafeDeque<PulseEvent>& pulse_queue) : gpio_pin_(gpio_pin), pulse_queue_(pulse_queue) {
}

FlowmeterComms::~FlowmeterComms()
{
    stop();
}

void FlowmeterComms::start()
{
}

void FlowmeterComms::stop()
{
}

void FlowmeterComms::read_loop()
{
}
