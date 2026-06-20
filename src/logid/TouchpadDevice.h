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

#ifndef LOGID_TOUCHPADDEVICE_H
#define LOGID_TOUCHPADDEVICE_H

#include <array>
#include <mutex>

extern "C"
{
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
}

namespace logid {
    class TouchpadDevice {
    public:
        explicit TouchpadDevice(const char* name);

        ~TouchpadDevice();

        TouchpadDevice(const TouchpadDevice&) = delete;

        TouchpadDevice& operator=(const TouchpadDevice&) = delete;

        void beginGesture(unsigned int fingers);

        void moveGesture(int dx, int dy);

        void endGesture();

        static constexpr unsigned int min_fingers = 3;
        static constexpr unsigned int max_fingers = 4;

    private:
        struct Contact {
            int x;
            int y;
            int tracking_id;
        };

        static constexpr int _max_contacts = 4;
        static constexpr int _touchpad_width = 16000;
        static constexpr int _touchpad_height = 10000;
        static constexpr int _touchpad_margin = 100;
        static constexpr int _touchpad_resolution = 100;
        static constexpr int _contact_spacing = 800;

        void _sendEvent(uint type, uint code, int value);

        void _sync();

        void _resetContacts(unsigned int fingers);

        void _writeContacts();

        void _setToolCount(unsigned int fingers, int value);

        void _endGestureUnlocked();

        static uint _toolCode(unsigned int fingers);

        libevdev* device;
        libevdev_uinput* ui_device{};

        std::mutex _input_mutex;
        bool _active{};
        unsigned int _fingers{};
        int _next_tracking_id{1};
        std::array<Contact, _max_contacts> _contacts{};
    };
}

#endif //LOGID_TOUCHPADDEVICE_H
