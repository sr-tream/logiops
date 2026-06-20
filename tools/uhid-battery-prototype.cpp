#include <linux/uhid.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace {

std::atomic_bool keep_running{true};

constexpr uint8_t kBatteryReportId = 1;
constexpr uint8_t kConsumerReportId = 2;

constexpr uint8_t kBatteryReportDescriptor[] = {
    0x05, 0x0c,              // Usage Page (Consumer)
    0x09, 0x01,              // Usage (Consumer Control)
    0xa1, 0x01,              // Collection (Application)
    0x85, kConsumerReportId, //   Report ID
    0x15, 0x00,              //   Logical Minimum (0)
    0x25, 0x01,              //   Logical Maximum (1)
    0x75, 0x01,              //   Report Size (1)
    0x95, 0x01,              //   Report Count (1)
    0x09, 0xe2,              //   Usage (Mute)
    0x81, 0x02,              //   Input (Data, Variable, Absolute)
    0x75, 0x07,              //   Report Size (7)
    0x95, 0x01,              //   Report Count (1)
    0x81, 0x03,              //   Input (Constant, Variable, Absolute)
    0xc0,                    // End Collection

    0x05, 0x06,             // Usage Page (Generic Device Controls)
    0x09, 0x20,             // Usage (Battery Strength)
    0xa1, 0x01,             // Collection (Application)
    0x85, kBatteryReportId, //   Report ID
    0x15, 0x00,             //   Logical Minimum (0)
    0x25, 0x64,             //   Logical Maximum (100)
    0x75, 0x08,             //   Report Size (8)
    0x95, 0x01,             //   Report Count (1)
    0x09, 0x20,             //   Usage (Battery Strength)
    0xb1, 0x02,             //   Feature (Data, Variable, Absolute)
    0xc0,                   // End Collection
};

struct Options {
  std::string name = "LogiOps UHID Battery Prototype";
  std::string phys = "logiops/uhid-battery";
  std::string uniq = "logiops-uhid-battery-prototype";
  uint8_t percent = 73;
  int duration_seconds = 60;
  int interval_seconds = 5;
};

void handle_signal(int) { keep_running = false; }

void usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n"
      << "\n"
      << "Options:\n"
      << "  --name TEXT       HID device name\n"
      << "  --phys TEXT       HID physical path string\n"
      << "  --uniq TEXT       HID unique string\n"
      << "  --percent N       Battery percentage, 0..100 (default: 73)\n"
      << "  --duration N      Seconds to keep the virtual device alive "
         "(default: 60)\n"
      << "  --interval N      Seconds between battery reports (default: 5)\n"
      << "  --help            Show this help\n";
}

bool parse_u8(const std::string &value, uint8_t &out) {
  try {
    size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed, 10);
    if (consumed != value.size() || parsed < 0 || parsed > 100)
      return false;
    out = static_cast<uint8_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_positive_int(const std::string &value, int &out) {
  try {
    size_t consumed = 0;
    const int parsed = std::stoi(value, &consumed, 10);
    if (consumed != value.size() || parsed <= 0)
      return false;
    out = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_args(int argc, char **argv, Options &options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const char *option) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << option << " requires a value\n";
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      return false;
    }
    if (arg == "--name") {
      const char *value = require_value("--name");
      if (!value)
        return false;
      options.name = value;
    } else if (arg == "--phys") {
      const char *value = require_value("--phys");
      if (!value)
        return false;
      options.phys = value;
    } else if (arg == "--uniq") {
      const char *value = require_value("--uniq");
      if (!value)
        return false;
      options.uniq = value;
    } else if (arg == "--percent") {
      const char *value = require_value("--percent");
      if (!value || !parse_u8(value, options.percent)) {
        std::cerr << "--percent must be an integer from 0 to 100\n";
        return false;
      }
    } else if (arg == "--duration") {
      const char *value = require_value("--duration");
      if (!value || !parse_positive_int(value, options.duration_seconds)) {
        std::cerr << "--duration must be a positive integer\n";
        return false;
      }
    } else if (arg == "--interval") {
      const char *value = require_value("--interval");
      if (!value || !parse_positive_int(value, options.interval_seconds)) {
        std::cerr << "--interval must be a positive integer\n";
        return false;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      usage(argv[0]);
      return false;
    }
  }

  return true;
}

void copy_string(uint8_t *dest, size_t size, const std::string &source) {
  if (size == 0)
    return;

  std::snprintf(reinterpret_cast<char *>(dest), size, "%s", source.c_str());
}

bool write_event(int fd, const uhid_event &event) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&event);
  size_t written = 0;

  while (written < sizeof(event)) {
    const ssize_t rc = write(fd, bytes + written, sizeof(event) - written);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "write(/dev/uhid) failed: " << std::strerror(errno) << "\n";
      return false;
    }
    written += static_cast<size_t>(rc);
  }

  return true;
}

bool send_create(int fd, const Options &options) {
  uhid_event event{};
  event.type = UHID_CREATE2;

  copy_string(event.u.create2.name, sizeof(event.u.create2.name), options.name);
  copy_string(event.u.create2.phys, sizeof(event.u.create2.phys), options.phys);
  copy_string(event.u.create2.uniq, sizeof(event.u.create2.uniq), options.uniq);

  event.u.create2.rd_size = sizeof(kBatteryReportDescriptor);
  std::memcpy(event.u.create2.rd_data, kBatteryReportDescriptor,
              sizeof(kBatteryReportDescriptor));

  event.u.create2.bus = BUS_BLUETOOTH;
  event.u.create2.vendor = 0x046d;
  event.u.create2.product = 0xc539;
  event.u.create2.version = 1;
  event.u.create2.country = 0;

  return write_event(fd, event);
}

bool send_destroy(int fd) {
  uhid_event event{};
  event.type = UHID_DESTROY;
  return write_event(fd, event);
}

bool send_consumer_idle_input(int fd) {
  uhid_event event{};
  event.type = UHID_INPUT2;
  event.u.input2.size = 2;
  event.u.input2.data[0] = kConsumerReportId;
  event.u.input2.data[1] = 0;
  return write_event(fd, event);
}

bool send_get_report_reply(int fd, uint32_t id, uint8_t percent) {
  uhid_event event{};
  event.type = UHID_GET_REPORT_REPLY;
  event.u.get_report_reply.id = id;
  event.u.get_report_reply.err = 0;
  event.u.get_report_reply.size = 2;
  event.u.get_report_reply.data[0] = kBatteryReportId;
  event.u.get_report_reply.data[1] = percent;
  return write_event(fd, event);
}

bool send_set_report_reply(int fd, uint32_t id) {
  uhid_event event{};
  event.type = UHID_SET_REPORT_REPLY;
  event.u.set_report_reply.id = id;
  event.u.set_report_reply.err = 0;
  return write_event(fd, event);
}

const char *event_name(uint32_t type) {
  switch (type) {
  case UHID_START:
    return "START";
  case UHID_STOP:
    return "STOP";
  case UHID_OPEN:
    return "OPEN";
  case UHID_CLOSE:
    return "CLOSE";
  case UHID_OUTPUT:
    return "OUTPUT";
  case UHID_GET_REPORT:
    return "GET_REPORT";
  case UHID_SET_REPORT:
    return "SET_REPORT";
  default:
    return "UNKNOWN";
  }
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parse_args(argc, argv, options))
    return 2;

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  const int fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    std::cerr << "open(/dev/uhid) failed: " << std::strerror(errno) << "\n";
    std::cerr << "Try loading the module with: pkexec modprobe uhid\n";
    return 1;
  }

  if (!send_create(fd, options)) {
    close(fd);
    return 1;
  }

  std::cout << "Created UHID battery device \"" << options.name << "\" at "
            << static_cast<int>(options.percent) << "% for "
            << options.duration_seconds << " seconds.\n";
  std::cout
      << "Inspect /sys/class/power_supply for a hid-*-battery-* device.\n";

  const auto started_at = std::chrono::steady_clock::now();
  auto next_report = started_at;

  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;

  while (keep_running) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - started_at);
    if (elapsed.count() >= options.duration_seconds)
      break;

    if (now >= next_report) {
      if (!send_consumer_idle_input(fd))
        break;
      std::cout << "Sent idle input report; battery feature report remains "
                << static_cast<int>(options.percent) << "%\n";
      next_report = now + std::chrono::seconds(options.interval_seconds);
    }

    const int rc = poll(&pfd, 1, 250);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "poll(/dev/uhid) failed: " << std::strerror(errno) << "\n";
      break;
    }
    if (rc == 0 || !(pfd.revents & POLLIN))
      continue;

    uhid_event event{};
    const ssize_t read_size = read(fd, &event, sizeof(event));
    if (read_size < 0) {
      if (errno == EINTR)
        continue;
      std::cerr << "read(/dev/uhid) failed: " << std::strerror(errno) << "\n";
      break;
    }
    if (read_size == 0) {
      std::cerr << "/dev/uhid closed by kernel\n";
      break;
    }

    std::cout << "UHID event: " << event_name(event.type) << " (" << event.type
              << ")\n";
    if (event.type == UHID_START || event.type == UHID_OPEN) {
      send_consumer_idle_input(fd);
    } else if (event.type == UHID_GET_REPORT) {
      send_get_report_reply(fd, event.u.get_report.id, options.percent);
    } else if (event.type == UHID_SET_REPORT) {
      send_set_report_reply(fd, event.u.set_report.id);
    }
  }

  send_destroy(fd);
  close(fd);
  std::cout << "Destroyed UHID battery device.\n";
  return 0;
}
