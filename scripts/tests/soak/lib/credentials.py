"""WiFi credential resolution for the reprovisioning step.

Reads SOAK_WIFI_SSID / SOAK_WIFI_PASSWORD env vars first (for unattended/CI
runs); falls back to interactive prompts at the exact moment they're needed
if unset. An empty password is valid (open network) -- only SSID is required.
"""
import getpass
import os


def get_wifi_credentials():
    ssid = os.environ.get("SOAK_WIFI_SSID")
    if not ssid:
        ssid = input("WiFi SSID to reprovision this board onto: ").strip()
    password = os.environ.get("SOAK_WIFI_PASSWORD")
    if password is None:
        password = getpass.getpass("WiFi password (blank for open network): ")
    return ssid, password
