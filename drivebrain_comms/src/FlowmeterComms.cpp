#include <FlowmeterComms.hpp>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <linux/gpio.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

FlowmeterComms::FlowmeterComms(unsigned int gpio_pin,core::ThreadSafeDeque<PulseEvent>& pulse_queue) : gpio_pin_(gpio_pin), pulse_queue_(pulse_queue) {}

FlowmeterComms::~FlowmeterComms() {
    stop();
}

void FlowmeterComms::start() {
    running_ = true;
    reader_thread_ = std::thread(&FlowmeterComms::read_loop, this);
    state_thread_ = std::thread(&FlowmeterComms::state_loop, this);
}

void FlowmeterComms::stop() {
    {
        std::lock_guard lock(pulse_queue_.mtx);
        running_ = false;
    }
    pulse_queue_.cv.notify_all();

    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
    if (state_thread_.joinable()) {
        state_thread_.join();
    }
}

FlowmeterState FlowmeterComms::wait_for_update(std::uint64_t last_seen_version) {

}

void FlowmeterComms::read_loop() {
    int fd = open("/dev/gpiochip0", O_RDONLY);
    gpio_v2_line_request request{};

    if (fd == -1) {
        perror("open /dev/gpiochip0");
        return;
    }
    request.offsets[0] = gpio_pin_;
    request.num_lines = 1;
    request.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING;

    int ctl = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &request);

    if (ctl == -1) {
        perror("GPIO_V2_GET_LINE_IOCTL");
        close(fd);
        return;
    }

    pollfd gpio_poll{};
    gpio_poll.fd = request.fd;
    gpio_poll.events = POLLIN;
    while (running_) {
        const int poll_result = poll(&gpio_poll, 1, 100);

        if (poll_result == 0) {
            continue;
        }

        if (poll_result == -1) {
            if (errno == EINTR) {
                continue;
            }

            perror("poll GPIO request");
            break;
        }

        if (gpio_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            std::fprintf(stderr, "GPIO poll reported error flags: 0x%x\n", gpio_poll.revents);
            break;
        }

        if (gpio_poll.revents & POLLIN) {
            gpio_v2_line_event event{};
            const ssize_t bytes_read = read(request.fd, &event, sizeof(event));

            if (bytes_read == -1) {
                if (errno == EINTR) {
                    continue;
                }

                perror("read GPIO event");
                break;
            }

            if (bytes_read != static_cast<ssize_t>(sizeof(event))) {
                std::fprintf(stderr, "Unexpected GPIO event read size: %zd\n", bytes_read);
                break;
            }

            {
                std::lock_guard<std::mutex> lock(pulse_queue_.mtx);
                pulse_queue_.deque.push_back(PulseEvent{event.timestamp_ns});
            }
            pulse_queue_.cv.notify_one();
        }
    }

    close(request.fd);
    close(fd);
}

void FlowmeterComms::state_loop() {
    std::deque<PulseEvent> temp_pulse_queue;
    FlowmeterState temp_state{};

    while (running_) {
        {
            std::unique_lock read_lk(pulse_queue_.mtx);
            pulse_queue_.cv.wait(read_lk, [this]() { return !pulse_queue_.deque.empty() || !running_; });

            if(!running_) {
                break;
            }

            pulse_queue_.deque.swap(temp_pulse_queue);
        }

        for (PulseEvent pulse : temp_pulse_queue){
            if (temp_state.total_pulses == 0) {
                temp_state.total_pulses = 1;
                temp_state.first_pulse_timestamp_ns = pulse.timestamp_ns;
                temp_state.last_pulse_timestamp_ns = pulse.timestamp_ns;
            } else {
                temp_state.total_pulses++;
                temp_state.last_pulse_timestamp_ns = pulse.timestamp_ns;
            }
        }
        {
            std::unique_lock state_lk(state_mutex_);
            temp_state.version = state_.version + 1
            state_ = temp_state;
        }

        temp_pulse_queue.clear();
    }
}
