/*
 * Copyright 2019-2023 PixlOne
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <actions/CommandAction.h>
#include <backend/hidpp20/features/ReprogControls.h>
#include <util/log.h>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace logid::actions;
using namespace logid::backend;

extern char** environ;

const char* CommandAction::interface_name = "Command";

CommandAction::CommandAction(
        Device* device, config::CommandAction& config,
        [[maybe_unused]] const std::shared_ptr<ipcgull::node>& parent) :
        Action(device, interface_name, {
                {
                        {"GetCommand", {this, &CommandAction::getCommand, {"command"}}},
                        {"SetCommand", {this, &CommandAction::setCommand, {"command"}}}
                },
                {},
                {}
        }), _config(config) {
}

void CommandAction::press() {
    _pressed = true;
}

void CommandAction::release() {
    if (_pressed)
        _execute();
    _pressed = false;
}

uint8_t CommandAction::reprogFlags() const {
    return hidpp20::ReprogControls::TemporaryDiverted;
}

std::vector<std::string> CommandAction::getCommand() const {
    std::shared_lock lock(_config_mutex);

    std::vector<std::string> command;
    if (!_config.command.has_value())
        return command;

    command.emplace_back(_config.command.value());
    if (_config.args.has_value()) {
        for (const auto& arg: _config.args.value())
            command.emplace_back(arg);
    }

    return command;
}

void CommandAction::setCommand(const std::vector<std::string>& command) {
    std::unique_lock lock(_config_mutex);

    if (command.empty()) {
        _config.command.reset();
        _config.args.reset();
        return;
    }

    _config.command = command.front();
    _config.args = std::list<std::string>();
    for (auto it = std::next(command.begin()); it != command.end(); ++it)
        _config.args->emplace_back(*it);
}

void CommandAction::_execute() const {
    auto command = getCommand();
    if (command.empty())
        return;

    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (auto& arg: command)
        argv.emplace_back(arg.data());
    argv.emplace_back(nullptr);

    pid_t pid = 0;
    int err = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (err != 0) {
        logPrintf(WARN, "Failed to execute command %s: %s",
                  command.front().c_str(), std::strerror(err));
        return;
    }

    std::thread([pid, command = command.front()]() {
        int status = 0;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) {
                logPrintf(WARN, "Failed to wait for command %s: %s",
                          command.c_str(), std::strerror(errno));
                return;
            }
        }
    }).detach();
}
