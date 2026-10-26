import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location(
    "host_switch", Path(__file__).with_name("logi-host-switch.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeBackend:
    def __init__(self):
        self.events = []
        self.current = 1
        self.fail_mouse = False
        self.fail_monitor = False

    def keyboard_host(self):
        return self.current

    def send_mouse(self, host):
        self.events.append(("mouse", host))
        if self.fail_mouse:
            raise RuntimeError("Mouse unavailable")

    def set_input(self, value):
        self.events.append(("monitor", value))
        if self.fail_monitor:
            raise RuntimeError("DDC unavailable")


class HostSwitchTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.state = Path(self.directory.name) / "state.json"
        self.config = module.load_config(Path(__file__).with_name("logi-host-switch.json"))
        self.backend = FakeBackend()
        self.controller = module.Controller(self.config, self.state, self.backend)

    def test_ps5_round_trip_and_duplicate_events(self):
        self.controller.switched(1, 0)
        self.controller.switched(1, 0)
        self.controller.connected()
        self.controller.connected()
        self.assertEqual(self.backend.events, [
            ("mouse", 0), ("monitor", "0x11"), ("monitor", "0x0f")])
        self.assertFalse(self.controller.away)

    def test_normal_wakeup_and_unrelated_host_do_nothing(self):
        self.controller.connected()
        self.controller.switched(1, 2)
        self.controller.switched(0, 1)
        self.assertEqual(self.backend.events, [])

    def test_mouse_failure_does_not_prevent_hdmi(self):
        self.backend.fail_mouse = True
        with self.assertLogs(module.LOG, level="ERROR"):
            self.controller.switched(1, 0)
        self.assertEqual(self.backend.events[-1], ("monitor", "0x11"))

    def test_monitor_failure_preserves_pending_return(self):
        self.controller.switched(1, 0)
        self.backend.fail_monitor = True
        with self.assertRaises(RuntimeError):
            self.controller.connected()
        self.assertTrue(self.controller.away)
        self.backend.fail_monitor = False
        self.controller.connected()
        self.assertFalse(self.controller.away)

    def test_restart_recovers_pending_return(self):
        self.controller.switched(1, 0)
        recovered = module.Controller(self.config, self.state, self.backend)
        recovered.connected()
        self.assertFalse(recovered.away)
        self.assertEqual(self.backend.events[-1], ("monitor", "0x0f"))

    def test_return_requires_local_keyboard_host(self):
        self.controller.switched(1, 0)
        self.backend.current = 0
        self.controller.connected()
        self.assertTrue(self.controller.away)
        self.assertEqual(self.backend.events[-1], ("monitor", "0x11"))

    def test_mouse_follow_can_be_disabled(self):
        self.config["follow_mouse"] = False
        self.controller.switched(1, 0)
        self.assertEqual(self.backend.events, [("monitor", "0x11")])

    def test_disconnect_fallback_is_opt_in(self):
        self.controller.disconnected()
        self.assertEqual(self.backend.events, [])
        self.config["departure_mode"] = "link_state"
        with self.assertLogs(module.LOG, level="WARNING"):
            self.controller.disconnected()
        self.assertEqual(self.backend.events, [("mouse", 0), ("monitor", "0x11")])
        self.controller.connected()
        self.assertEqual(self.backend.events[-1], ("monitor", "0x0f"))

    def test_new_link_loss_retries_after_failed_previous_round_trip(self):
        self.config["departure_mode"] = "link_state"
        self.backend.fail_monitor = True
        with self.assertLogs(module.LOG, level="WARNING"), self.assertRaises(RuntimeError):
            self.controller.disconnected()
        # A failed departure persists away=True. A later real link loss must
        # still send both commands rather than swallowing the next switch.
        self.backend.fail_monitor = False
        self.backend.events.clear()
        with self.assertLogs(module.LOG, level="WARNING"):
            self.controller.disconnected()
        self.assertEqual(self.backend.events, [("mouse", 0), ("monitor", "0x11")])

    def test_link_state_return_does_not_wait_for_hidpp_host_query(self):
        self.config["departure_mode"] = "link_state"
        self.controller.save(True)
        self.backend.keyboard_host = Mock(side_effect=TimeoutError("Keyboard waking up"))
        self.controller.connected()
        self.backend.keyboard_host.assert_not_called()
        self.assertEqual(self.backend.events, [("monitor", "0x0f")])
        self.assertFalse(self.controller.away)

    def test_reconnection_notification_requires_active_link(self):
        daemon = module.Daemon.__new__(module.Daemon)
        daemon.controller = Mock(away=True, config=self.config)
        daemon.backend = Mock(keyboard="/keyboard", devices={"/keyboard": {"Active": False}})
        daemon.connected("/keyboard")
        daemon.controller.connected.assert_not_called()
        daemon.backend.devices["/keyboard"]["Active"] = True
        daemon.connected("/keyboard")
        daemon.controller.connected.assert_called_once()

    def test_early_link_event_restores_monitor_before_reconfiguration(self):
        daemon = module.Daemon.__new__(module.Daemon)
        self.config["departure_mode"] = "link_state"
        daemon.controller = Mock(away=True, config=self.config)
        daemon.backend = Mock(keyboard="/keyboard", devices={"/keyboard": {"Active": False}})
        daemon.connected("/keyboard", link_event=True)
        daemon.controller.connected.assert_called_once()

    def test_bus_selection_survives_renumbering_and_rejects_ambiguity(self):
        root = Path(self.directory.name)
        for number, name in ((3, "other monitor"), (7, self.config["ddc_adapter"])):
            (root / f"i2c-{number}").mkdir()
            (root / f"i2c-{number}" / "name").write_text(name + "\n")
        self.assertEqual(module.resolve_bus(self.config["ddc_adapter"], root), 7)
        (root / "i2c-3" / "name").write_text(self.config["ddc_adapter"])
        with self.assertRaises(RuntimeError):
            module.resolve_bus(self.config["ddc_adapter"], root)


if __name__ == "__main__":
    unittest.main()
