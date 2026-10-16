#!/usr/bin/env python3
"""
Xiegu X6100 - Birdie & Spur Auto-Calibration Tool
by Antigravity / X6100-IU1WXY Community

This script connects to the X6100 (either locally via /tmp/x6100_remote.sock
or over Wi-Fi via the /link/v2 HTTP API) and automatically scans frequencies
with the antenna disconnected or dummy load attached.

When an internal birdie/spur carrier exceeds the noise floor threshold,
it records the frequency, signal level (dBm), and SNR into /mnt/birdies.json.

Usage:
  # On the radio directly via local UNIX socket:
  python3 birdie_cal.py --local

  # Over Wi-Fi from PC / phone:
  python3 birdie_cal.py --host 192.168.1.100 --band 20m --step 5000 --thresh 10
"""

import argparse
import json
import os
import socket
import sys
import time
import urllib.request
import urllib.error

BANDS = {
    "160m":   (1800000, 2000000),
    "80m":    (3500000, 3800000),
    "40m":    (7000000, 7200000),
    "30m":    (10100000, 10150000),
    "20m":    (14000000, 14350000),
    "17m":    (18068000, 18168000),
    "15m":    (21000000, 21450000),
    "12m":    (24890000, 24990000),
    "10m":    (28000000, 29700000),
    "6m":     (50000000, 52000000),
    "all_hf": (1800000, 29700000),
}

REMOTE_SOCK = "/tmp/x6100_remote.sock"
OUTPUT_FILE = "/mnt/birdies.json" if os.path.exists("/mnt") else "birdies.json"


class RadioClient:
    def __init__(self, host=None, local=False):
        self.host = host
        self.local = local
        self.sock = None

    def connect(self):
        if self.local:
            if not os.path.exists(REMOTE_SOCK):
                raise RuntimeError(f"Socket {REMOTE_SOCK} not found. Is X6100 GUI running?")
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.settimeout(5.0)
            self.sock.connect(REMOTE_SOCK)
        else:
            # Test HTTP connection
            url = f"http://{self.host}/link/v2/info"
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "BirdieCal/1.0"})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    data = json.loads(resp.read().decode())
                    print(f"[*] Connected to Xiegu X6100: radio={data.get('radio')}, callsign={data.get('callsign')}")
            except Exception as e:
                raise RuntimeError(f"Could not connect to http://{self.host}/link/v2/info: {e}")

    def send_cmd(self, cmd, val=None, text=None):
        payload = {"cmd": cmd}
        if val is not None:
            payload["val"] = val
        if text is not None:
            payload["text"] = text

        if self.local:
            msg = json.dumps(payload).encode() + b"\n"
            self.sock.sendall(msg)
            reply = self.sock.makefile("rb").readline()
            return json.loads(reply.decode()) if reply else {}
        else:
            url = f"http://{self.host}/link/v2/cmd"
            req = urllib.request.Request(
                url,
                data=json.dumps(payload).encode("utf-8"),
                headers={"Content-Type": "application/json"}
            )
            try:
                with urllib.request.urlopen(req, timeout=3) as resp:
                    return json.loads(resp.read().decode())
            except urllib.error.HTTPError as e:
                return {"err": e.read().decode()}

    def get_state(self):
        if self.local:
            msg = json.dumps({"cmd": "get"}).encode() + b"\n"
            self.sock.sendall(msg)
            reply = self.sock.makefile("rb").readline()
            return json.loads(reply.decode()) if reply else {}
        else:
            # Read 1 event from SSE stream
            url = f"http://{self.host}/link/v2/events"
            req = urllib.request.Request(url)
            with urllib.request.urlopen(req, timeout=5) as resp:
                for line in resp:
                    line = line.decode("utf-8").strip()
                    if line.startswith("data:"):
                        raw = line[5:].strip()
                        data = json.loads(raw)
                        if data.get("t") == "state":
                            return data
            return {}


def run_calibration(client, band_name, step_hz, thresh_db, output_file):
    if band_name not in BANDS:
        print(f"Unknown band {band_name}. Available: {', '.join(BANDS.keys())}")
        return

    start_f, stop_f = BANDS[band_name]
    total_steps = (stop_f - start_f) // step_hz
    print(f"\n=======================================================")
    print(f" Xiegu X6100 Birdie Auto-Calibration")
    print(f" Band:       {band_name} ({start_f/1e6:.3f} - {stop_f/1e6:.3f} MHz)")
    print(f" Step size:  {step_hz} Hz ({total_steps} steps)")
    print(f" Threshold:  +{thresh_db} dB above local noise floor")
    print(f" Target file: {output_file}")
    print(f" WARNING:    Make sure antenna is DISCONNECTED or DUMMY LOAD attached!")
    print(f"=======================================================\n")

    input("Press Enter to begin calibration (Ctrl+C to abort)...")

    # Set mode to USB
    client.send_cmd("mode", "USB")

    birdies = []
    noise_floor = -125.0
    cur_f = start_f

    try:
        idx = 0
        while cur_f <= stop_f:
            client.send_cmd("freq", cur_f)
            time.sleep(0.06)  # 60ms settling time for AGC & baseband

            state = client.get_state()
            raw_db = state.get("db", -120)

            # Update noise floor (exponential minimum tracker)
            if raw_db < noise_floor:
                noise_floor = 0.8 * noise_floor + 0.2 * raw_db
            else:
                noise_floor = 0.99 * noise_floor + 0.01 * raw_db

            diff = raw_db - noise_floor

            # Check if this is a spur
            if diff >= thresh_db:
                # Merge adjacent spurs within 1.5 kHz
                if not any(abs(cur_f - b["freq"]) < 1500 for b in birdies):
                    birdies.append({
                        "freq": cur_f,
                        "db": raw_db,
                        "snr": round(diff, 1)
                    })
                    print(f"  [!] SPUR #{len(birdies)}: {cur_f/1e6:.6f} MHz | {raw_db} dBm (+{diff:.1f} dB)")

            idx += 1
            if idx % 20 == 0 or cur_f >= stop_f:
                pct = ((cur_f - start_f) / (stop_f - start_f)) * 100
                print(f"  Progress: {pct:5.1f}% | Freq: {cur_f/1e6:.3f} MHz | Noise: {noise_floor:.1f} dBm | Found: {len(birdies)} spurs", end="\r")

            cur_f += step_hz

    except KeyboardInterrupt:
        print("\n\nScan stopped by user.")

    print(f"\n\n[*] Scan finished! Total internal birdies found: {len(birdies)}")

    # Save to JSON
    result = {
        "timestamp": int(time.time()),
        "band": band_name,
        "step_hz": step_hz,
        "thresh_db": thresh_db,
        "count": len(birdies),
        "birdies": birdies
    }

    with open(output_file, "w") as fp:
        json.dump(result, fp, indent=2)

    print(f"[*] Saved calibration results to: {output_file}\n")


def main():
    parser = argparse.ArgumentParser(description="X6100 Birdie Auto-Calibration Tool")
    parser.add_argument("--host", default="127.0.0.1", help="X6100 IP address (if over Wi-Fi)")
    parser.add_argument("--local", action="store_true", help="Connect via local UNIX socket /tmp/x6100_remote.sock on radio")
    parser.add_argument("--band", default="20m", choices=list(BANDS.keys()), help="Band to scan")
    parser.add_argument("--step", type=int, default=5000, help="Step size in Hz (default: 5000)")
    parser.add_argument("--thresh", type=int, default=10, help="Spur detection threshold in dB (default: 10)")
    parser.add_argument("--out", default=OUTPUT_FILE, help="Output JSON file path")
    args = parser.parse_args()

    client = RadioClient(host=args.host, local=args.local)
    try:
        client.connect()
    except Exception as e:
        print(f"Connection failed: {e}")
        sys.exit(1)

    run_calibration(client, args.band, args.step, args.thresh, args.out)


if __name__ == "__main__":
    main()
