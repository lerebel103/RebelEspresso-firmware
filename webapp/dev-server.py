#!/usr/bin/env python3
"""
Local development server for the RebelEspresso web UI.

Serves the frontend and provides mock API responses so you can iterate
on the UI without hardware. Run from the project root:

    python3 webapp/dev-server.py

Then open http://localhost:8080
"""
import http.server
import json
import os
import time
import random

PORT = 8080
WEBAPP_DIR = os.path.dirname(os.path.abspath(__file__))

# Mock data
MOCK_STATUS = {
    "temperatures": {
        "boiler": 104.2,
        "boiler_fault": 0,
        "boiler_setpoint": 105.0,
        "boiler_duty": 72,
        "brew_head": 92.8,
        "brew_head_fault": 0,
        "brew_head_setpoint": 93.5,
    },
    "power_active": True,
    "boiler_level": "ok",
    "wifi": {"ssid": "HomeNetwork", "rssi": -52, "channel": 6},
    "uptime_sec": 3842,
    "free_heap": 142000,
}

MOCK_CONFIGS = {
    "boiler_temp": {
        "pid.P": 7.0, "pid.I": 0.5, "pid.D": 170.0,
        "pid.I_reset_temp": 5.0,
        "pid.setpoint0": 105.0, "pid.setpoint1": 140.0,
        "pid.over_setpoint_perc": 8.0,
        "mains_hz": 50, "temp_error_restart_time_sec": 60,
        "full_duty_pid_error_threshold": 10,
    },
    "brew_temp": {
        "pid.P": 3.0, "pid.I": 0.2, "pid.D": 80.0,
        "pid.setpoint0": 93.5, "pid.setpoint1": 93.5,
        "pid.over_setpoint_perc": 5.0,
        "enabled": True, "max_damping_perc": 10.0,
        "boiler_setpoint_hold_sec": 180.0,
    },
    "boiler_refill": {
        "start_delay_ms": 1000, "stabilise_ms": 5,
        "adc_num_readings": 25, "refill_mv_threshold": 2400,
        "max_refill_time_ms": 15000,
        "level_low_hysteresis_ms": 750, "level_ok_hysteresis_ms": 1000,
    },
    "schedules": {
        "en": True,
        "mon": [{"en": True, "start": "06:30", "stop": "22:00"}],
        "tue": [{"en": False, "start": "00:00", "stop": "00:00"}],
        "wed": [{"en": False, "start": "00:00", "stop": "00:00"}],
        "thu": [{"en": False, "start": "00:00", "stop": "00:00"}],
        "fri": [{"en": False, "start": "00:00", "stop": "00:00"}],
        "sat": [{"en": True, "start": "08:00", "stop": "23:00"}],
        "sun": [{"en": True, "start": "09:00", "stop": "21:00"}],
    },
}

MOCK_SYSTEM_INFO = {
    "firmware_version": "1.2.0",
    "project_name": "coffee-drivah-firmware",
    "idf_version": "v6.0.2",
    "build_date": "Jul 31 2026",
    "build_time": "10:30:00",
    "thing_type": "rebel-espresso",
    "thing_id": "espresso-001",
    "hardware_rev": 2,
    "boot_count": 42,
    "crash_count": 1,
    "free_heap": 142000,
    "min_free_heap": 98000,
    "uptime_sec": 3842,
}

MOCK_AUTH = {"enabled": False}


class MockAPIHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEBAPP_DIR, **kwargs)

    def do_GET(self):
        if self.path == "/api/status":
            # Add some randomness to simulate live data
            data = MOCK_STATUS.copy()
            data["temperatures"] = MOCK_STATUS["temperatures"].copy()
            data["temperatures"]["boiler"] += random.uniform(-0.3, 0.3)
            data["temperatures"]["brew_head"] += random.uniform(-0.2, 0.2)
            data["temperatures"]["boiler_duty"] = max(0, min(100, MOCK_STATUS["temperatures"]["boiler_duty"] + random.randint(-5, 5)))
            data["uptime_sec"] = int(time.time() - START_TIME)
            self._json_response(data)
        elif self.path.startswith("/api/config/"):
            name = self.path.split("/api/config/")[1].split("/")[0]
            if name in MOCK_CONFIGS:
                self._json_response(MOCK_CONFIGS[name])
            else:
                self.send_error(404, f"Unknown config: {name}")
        elif self.path == "/api/system/info":
            info = MOCK_SYSTEM_INFO.copy()
            info["uptime_sec"] = int(time.time() - START_TIME)
            self._json_response(info)
        elif self.path == "/api/auth":
            self._json_response(MOCK_AUTH)
        else:
            super().do_GET()

    def do_PUT(self):
        if self.path.startswith("/api/config/"):
            name = self.path.split("/api/config/")[1].split("/")[0]
            body = self._read_body()
            if body and name in MOCK_CONFIGS:
                data = json.loads(body)
                MOCK_CONFIGS[name].update(data)
                print(f"  Config '{name}' updated: {data}")
                self._json_response({"status": "ok"})
            else:
                self.send_error(400, "Invalid request")
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path.endswith("/reset"):
            name = self.path.split("/api/config/")[1].split("/")[0]
            print(f"  Config '{name}' reset to defaults")
            self._json_response({"status": "ok", "message": "reset to defaults"})
        elif self.path == "/api/system/ota":
            print("  OTA upload received (mock — not actually flashing)")
            self._json_response({"status": "ok", "message": "OTA complete (mock)"})
        elif self.path == "/api/system/reboot":
            print("  Reboot requested (mock)")
            self._json_response({"status": "ok", "message": "Rebooting (mock)"})
        elif self.path == "/api/system/factory-reset":
            print("  Factory reset requested (mock)")
            self._json_response({"status": "ok", "message": "Factory reset (mock)"})
        elif self.path == "/api/auth":
            body = self._read_body()
            if body:
                data = json.loads(body)
                MOCK_AUTH["enabled"] = True
                print(f"  Auth enabled with password (mock)")
            self._json_response({"status": "ok", "message": "Password set"})
        else:
            self.send_error(404)

    def do_DELETE(self):
        if self.path == "/api/auth":
            MOCK_AUTH["enabled"] = False
            print("  Auth disabled (mock)")
            self._json_response({"status": "ok", "message": "Auth disabled"})
        else:
            self.send_error(404)

    def _json_response(self, data):
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        if length > 0:
            return self.rfile.read(length).decode()
        return None

    def log_message(self, format, *args):
        # Quieter logging — only show API calls
        msg = format % args
        if "/api/" in msg:
            print(f"  {msg}")


START_TIME = time.time()

if __name__ == "__main__":
    print(f"RebelEspresso dev server running at http://localhost:{PORT}")
    print(f"Serving UI from: {WEBAPP_DIR}")
    print(f"API calls return mock data. Press Ctrl+C to stop.\n")

    server = http.server.HTTPServer(("", PORT), MockAPIHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
