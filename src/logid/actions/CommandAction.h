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

#ifndef LOGID_ACTION_COMMAND_H
#define LOGID_ACTION_COMMAND_H

#include <actions/Action.h>
#include <vector>

namespace logid::actions {
    class CommandAction : public Action {
    public:
        static const char* interface_name;

        CommandAction(Device* dev,
                      config::CommandAction& config,
                      const std::shared_ptr<ipcgull::node>& parent);

        void press() final;

        void release() final;

        [[nodiscard]] std::vector<std::string> getCommand() const;

        void setCommand(const std::vector<std::string>& command);

        [[nodiscard]] uint8_t reprogFlags() const final;

    private:
        void _execute() const;

        config::CommandAction& _config;
    };
}

#endif //LOGID_ACTION_COMMAND_H
