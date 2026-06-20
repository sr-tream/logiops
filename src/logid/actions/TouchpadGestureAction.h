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

#ifndef LOGID_ACTION_TOUCHPADGESTUREACTION_H
#define LOGID_ACTION_TOUCHPADGESTUREACTION_H

#include <actions/Action.h>
#include <tuple>

namespace logid::actions {
    class TouchpadGestureAction : public Action {
    public:
        static const char* interface_name;

        TouchpadGestureAction(Device* device, config::TouchpadGestureAction& config,
                              const std::shared_ptr<ipcgull::node>& parent);

        void press() final;

        void release() final;

        void move(int16_t x, int16_t y) final;

        [[nodiscard]] uint8_t reprogFlags() const final;

        [[nodiscard]] std::tuple<unsigned int, double, bool, int> getConfig() const;

        void setFingers(unsigned int fingers);

        void setScale(double scale);

        void setInvert(bool invert);

        void setClickThreshold(int threshold);

    private:
        [[nodiscard]] unsigned int _fingers() const;

        [[nodiscard]] double _scale() const;

        [[nodiscard]] bool _invert() const;

        [[nodiscard]] int _clickThreshold() const;

        void _beginTouchpadGesture();

        std::shared_ptr<ipcgull::node> _click_node;
        std::shared_ptr<Action> _click_action;

        int32_t _movement{};
        int _pending_x{};
        int _pending_y{};
        bool _touchpad_active{};

        config::TouchpadGestureAction& _config;
    };
}

#endif //LOGID_ACTION_TOUCHPADGESTUREACTION_H
