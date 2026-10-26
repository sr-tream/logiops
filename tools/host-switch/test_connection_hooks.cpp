// SPDX-License-Identifier: GPL-3.0-or-later
#include <config/schema.h>
#include <util/CommandQueue.h>
#include <util/log.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>

logid::LogLevel logid::global_loglevel = logid::WARN;

static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            libconfig::Config file;
            file.readFile(argv[1]);
            auto config = logid::config::get<logid::config::Config>(file.getRoot());
            require(config.devices.has_value(), "No devices in config");
            std::cout << "Configuration parsed\n";
            return 0;
        }
        libconfig::Config file;
        file.readString(R"(
            flat = { on_connect = { command = "/bin/echo"; args = ["hello"]; };
                     dpi = 1200; };
            nested = { default_profile = "default";
                       profiles = ({ name = "default"; dpi = 1600; });
                       on_disconnect = { command = "/bin/true"; }; };
        )");
        auto flat = logid::config::get<logid::config::Profile>(file.getRoot(), "flat");
        require(flat.on_connect && flat.on_connect->command == "/bin/echo",
                "Flat hook not parsed");
        require(flat.on_connect->args->front() == "hello", "Hook arguments lost");
        auto nested = logid::config::get<logid::config::Device>(file.getRoot(), "nested");
        require(nested.on_disconnect && nested.on_disconnect->command == "/bin/true",
                "Device hook not parsed");
        require(nested.profiles.count("default") == 1, "Existing profiles lost");
        file.getRoot().add("saved", libconfig::Setting::TypeGroup);
        logid::config::set(file.lookup("saved"), nested);
        auto saved = logid::config::get<logid::config::Device>(file.getRoot(), "saved");
        require(saved.on_disconnect && saved.on_disconnect->command == "/bin/true",
                "Hook lost on config save");

        char directory[] = "/tmp/logi-hooks-test-XXXXXX";
        require(mkdtemp(directory) != nullptr, "mkdtemp failed");
        const auto output = std::string(directory) + "/events";
        const std::string literal = "literal;$(false) \"$HOME\"";
        {
            logid::CommandQueue queue;
            queue.run({"/bin/sh", "-c", "sleep .2; printf '%s\\n' \"$1\" >> \"$2\"",
                       "hook", literal, output}, "connect test");
            queue.run({"/missing/logi-hooks-test-command"}, "spawn failure test");
            queue.run({"/bin/sh", "-c", "exit 7"}, "exit failure test");
            queue.run({"/bin/sh", "-c", "printf '%s\\n' second >> \"$1\"",
                       "hook", output}, "disconnect test");
            // Destruction must not wait for children or discard queued work.
        }
        std::string first, second;
        for (int i = 0; i < 500; ++i) {
            std::ifstream stream(output);
            if (std::getline(stream, first) && std::getline(stream, second))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::filesystem::remove_all(directory);
        require(first == literal, "Arguments interpreted or command ordering broken");
        require(second == "second", "Queue stopped after command error");
        std::cout << "Connection hook config and process ordering passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
