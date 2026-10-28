#!/usr/bin/env python3
"""
DeskBot High-Performance Threaded Companion Server
Blazing fast multi-threaded server with asynchronous background Spotify polling.
Zero latency, zero blocking, instant response times.
"""

import http.server
import socketserver
import subprocess
import json
import urllib.parse
import os
import threading
import time

PORT = 3000
WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")

# In-memory cache for instant zero-latency responses
spotify_cache = {
    "active": False,
    "title": "No Track",
    "artist": "Spotify Idle",
    "playing": False,
    "position": 0,
    "duration": 180
}
cache_lock = threading.Lock()

# Spotify Web API Credentials
SPOTIFY_CLIENT_ID = "001f2f27609349db91cea22353df81ce"
SPOTIFY_CLIENT_SECRET = "9e26492500724b659b5a3ae35d389752"
SPOTIFY_REFRESH_TOKEN = "AQAqe4VQuN7ZnP-mE1rnsICJg_-JBk1W0ZGa2qVx0h5lB-KjorgUhXj3qJ4FhSDIo0XqslcfNyVBbg30RfSRVgEz4SEd01fBE-Jr2NOcQoJSFi_LxYgCbqLyK1STx1ZgyV4"

spotify_access_token = ""
spotify_token_expires = 0

def refresh_spotify_token():
    """Refreshes the Spotify Web API access token."""
    global spotify_access_token, spotify_token_expires
    try:
        import urllib.request, urllib.parse, base64
        auth = base64.b64encode(f"{SPOTIFY_CLIENT_ID}:{SPOTIFY_CLIENT_SECRET}".encode()).decode()
        data = urllib.parse.urlencode({
            "grant_type": "refresh_token",
            "refresh_token": SPOTIFY_REFRESH_TOKEN
        }).encode()
        req = urllib.request.Request(
            "https://accounts.spotify.com/api/token",
            data=data,
            headers={
                "Authorization": f"Basic {auth}",
                "Content-Type": "application/x-www-form-urlencoded"
            }
        )
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            d = json.loads(resp.read().decode())
            spotify_access_token = d.get("access_token", "")
            spotify_token_expires = time.time() + d.get("expires_in", 3600) - 60
            return True
    except Exception as e:
        return False

def query_spotify_cloud():
    """Queries Spotify Web API for current playback across any device."""
    global spotify_access_token, spotify_token_expires
    if not spotify_access_token or time.time() > spotify_token_expires:
        if not refresh_spotify_token():
            return None

    try:
        import urllib.request
        req = urllib.request.Request(
            "https://api.spotify.com/v1/me/player",
            headers={"Authorization": f"Bearer {spotify_access_token}"}
        )
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            if resp.status == 200:
                d = json.loads(resp.read().decode())
                item = d.get("item") or {}
                artists = ", ".join([a.get("name", "") for a in item.get("artists", [])])
                dev_name = (d.get("device") or {}).get("name", "Active Device")
                dur_sec = (item.get("duration_ms") or 180000) // 1000
                pos_sec = (d.get("progress_ms") or 0) // 1000
                return {
                    "active": True,
                    "source": "cloud_api",
                    "device": dev_name,
                    "title": item.get("name", "Unknown Track"),
                    "artist": artists or "Unknown Artist",
                    "playing": bool(d.get("is_playing", False)),
                    "position": pos_sec,
                    "duration": dur_sec if dur_sec > 0 else 180
                }
            elif resp.status == 204:
                return {"active": False, "source": "cloud_idle"}
    except Exception:
        pass
    return None

def control_spotify_cloud(action):
    """Sends control commands to Spotify Web API."""
    global spotify_access_token
    if not spotify_access_token or time.time() > spotify_token_expires:
        refresh_spotify_token()
    if not spotify_access_token:
        return False

    import urllib.request
    method = "PUT"
    endpoint = "play"
    if action == "pause":
        endpoint = "pause"
    elif action == "playpause":
        with cache_lock:
            endpoint = "pause" if spotify_cache.get("playing") else "play"
    elif action == "next":
        endpoint = "next"
        method = "POST"
    elif action == "prev":
        endpoint = "previous"
        method = "POST"

    url = f"https://api.spotify.com/v1/me/player/{endpoint}"
    try:
        req = urllib.request.Request(
            url,
            data=b"",
            headers={"Authorization": f"Bearer {spotify_access_token}", "Content-Length": "0"},
            method=method
        )
        with urllib.request.urlopen(req, timeout=3.0) as resp:
            return resp.status in (200, 204)
    except Exception:
        return False

def spotify_worker_thread():
    """Background worker that queries Spotify Web API and macOS Spotify asynchronously."""
    global spotify_cache
    apple_script = '''
    if application "Spotify" is running then
        tell application "Spotify"
            try
                set trackName to name of current track
                set trackArtist to artist of current track
                set playState to player state as string
                set pos to player position
                set dur to duration of current track
                return trackName & "|||" & trackArtist & "|||" & playState & "|||" & pos & "|||" & dur
            on error
                return "INACTIVE"
            end try
        end tell
    else
        return "NOT_RUNNING"
    end if
    '''
    while True:
        try:
            # 1. First priority: Check Spotify Web API (works across iPhone, Mac, Web, Alexa, etc.)
            cloud_state = query_spotify_cloud()
            if cloud_state and cloud_state.get("active"):
                with cache_lock:
                    spotify_cache = cloud_state
                time.sleep(1.2)
                continue

            # 2. Second priority: Local macOS Spotify application via AppleScript
            res = subprocess.run(['osascript', '-e', apple_script], capture_output=True, text=True, timeout=1.0)
            out = res.stdout.strip()
            if "|||" in out:
                parts = out.split("|||")
                dur_ms = float(parts[4]) if len(parts) > 4 else 0
                dur_sec = int(dur_ms / 1000) if dur_ms > 1000 else int(dur_ms)
                new_data = {
                    "active": True,
                    "source": "mac_desktop",
                    "device": "MacBook",
                    "title": parts[0],
                    "artist": parts[1],
                    "playing": (parts[2].lower() == "playing"),
                    "position": int(float(parts[3])),
                    "duration": dur_sec if dur_sec > 0 else 180
                }
                with cache_lock:
                    spotify_cache = new_data
            else:
                with cache_lock:
                    spotify_cache = {
                        "active": False,
                        "source": "idle",
                        "device": "None",
                        "title": "Spotify Idle",
                        "artist": "No Active Playback",
                        "playing": False,
                        "position": 0,
                        "duration": 180
                    }
        except Exception:
            pass
        time.sleep(1.2)

def control_spotify(action):
    """Controls Spotify playback via Cloud API or macOS AppleScript."""
    source = ""
    with cache_lock:
        source = spotify_cache.get("source", "")

    if source == "cloud_api":
        if control_spotify_cloud(action):
            return True

    # Fallback to macOS AppleScript
    script = ""
    if action == "play":
        script = 'tell application "Spotify" to play'
    elif action == "pause":
        script = 'tell application "Spotify" to pause'
    elif action == "playpause":
        script = 'tell application "Spotify" to playpause'
    elif action == "next":
        script = 'tell application "Spotify" to next track'
    elif action == "prev":
        script = 'tell application "Spotify" to previous track'

    if script:
        try:
            subprocess.run(['osascript', '-e', script], timeout=1.0)
            return True
        except Exception:
            pass
    return False

class DeskBotHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def log_message(self, format, *args):
        # Silence verbose logging for maximum performance
        pass

    def end_headers(self):
        # Enable CORS for local API access
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', '*')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        
        # Endpoint: Instant cached Spotify telemetry (< 1ms latency!)
        if parsed.path == "/api/spotify-mac":
            with cache_lock:
                data = dict(spotify_cache)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(data).encode('utf-8'))
            return

        # Endpoint: Real-time control
        if parsed.path == "/api/spotify-mac/control":
            params = urllib.parse.parse_qs(parsed.query)
            act = params.get("action", ["playpause"])[0]
            ok = control_spotify(act)
            with cache_lock:
                data = dict(spotify_cache)
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({"success": ok, "status": data}).encode('utf-8'))
            return

        super().do_GET()

class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

if __name__ == "__main__":
    # Start background polling thread
    t = threading.Thread(target=spotify_worker_thread, daemon=True)
    t.start()

    server = ThreadedHTTPServer(("", PORT), DeskBotHandler)
    print(f"🚀 DeskBot High-Performance Server running on http://localhost:{PORT}")
    server.serve_forever()
