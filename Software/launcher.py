"""
TerraSecure Desktop Launcher
============================
Starts the Flask backend on a background thread and opens the HMI inside a
native pywebview window (Edge WebView2 on Windows). No browser tab needed.

Run directly:
    python launcher.py

Build a standalone EXE (no console):
    pyinstaller --noconsole --onefile --name TerraSecure launcher.py
"""
import os
import socket
import sys
import threading
import time
import urllib.request

import webview

from app import app

HOST = "127.0.0.1"
WINDOW_TITLE = "TerraSecure - Rover-Based Landmine Detection & Mapping"


def find_free_port():
    """Ask the OS for an available port on 127.0.0.1."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, 0))
        return s.getsockname()[1]


def wait_for_server(url, timeout=10.0):
    """Poll the backend root until it answers or the timeout elapses."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            urllib.request.urlopen(url, timeout=1)
            return True
        except Exception:
            time.sleep(0.1)
    return False


def main():
    port = find_free_port()
    url = f"http://{HOST}:{port}"

    # Flask backend on a background daemon thread (dying with the process)
    def serve():
        try:
            app.run(host=HOST, port=port, debug=False, threaded=True, use_reloader=False)
        except Exception as e:
            print(f"Server error: {e}")

    threading.Thread(target=serve, daemon=True).start()

    if not wait_for_server(url):
        print("Backend failed to start.")
        sys.exit(1)

    # Open the native webview window (no browser required)
    try:
        webview.create_window(WINDOW_TITLE, url)
        webview.start()
    except Exception as e:
        # Fallback: open the default browser so the app is still usable
        import webbrowser
        print(f"Webview failed to start ({e}). Opening default browser instead.")
        webbrowser.open(url)
        try:
            input("Press Enter to exit...")
        except EOFError:
            time.sleep(3600)


if __name__ == "__main__":
    main()