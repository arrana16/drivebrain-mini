#include <FlowmeterComms.hpp>
#include <cerrno>
#include <cstddef>
#include <cstdio>
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
}

void FlowmeterComms::stop()
{
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
            continue; // Timeout: recheck running_.
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
