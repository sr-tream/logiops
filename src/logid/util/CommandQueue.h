// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LOGID_COMMAND_QUEUE_H
#define LOGID_COMMAND_QUEUE_H

#include <memory>
#include <string>
#include <vector>

namespace logid {
    // Commands run in order without blocking HID I/O or D-Bus dispatch.
    // A running child owns the queue state independently of its Device.
    class CommandQueue {
    public:
        CommandQueue();
        void run(std::vector<std::string> command, std::string context);

    private:
        struct State;
        std::shared_ptr<State> _state;
    };
}
#endif
