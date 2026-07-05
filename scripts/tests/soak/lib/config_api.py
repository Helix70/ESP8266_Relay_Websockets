"""Wrappers for /api/config, /api/clearwifi, /api/reboot, /api/wifi/rescan,
and /api/labels, per src/config_system_routes.cpp.
"""


def set_board_config(http, name, use_dhcp, ip=None, dns=None, gateway=None, subnet=None,
                      do_delay=None, connect_strongest=None, delay_seconds=None):
    fields = {"name": name, "useDhcp": "true" if use_dhcp else "false"}
    if not use_dhcp:
        fields.update({"ip": ip, "dns": dns, "gateway": gateway, "subnet": subnet})
    if do_delay is not None:
        fields["doDelay"] = "true" if do_delay else "false"
    if connect_strongest is not None:
        fields["connectStrongestOnStartup"] = "true" if connect_strongest else "false"
    if delay_seconds is not None:
        fields["delaySeconds"] = str(delay_seconds)
    return http.post("/api/config", fields)


def long_board_name_example():
    """Exceeds kMaxBoardNameLength (64) -- expected to be silently truncated
    to 64 chars server-side, not rejected."""
    return "Soak Test Board Name That Is Deliberately Far Too Long For The Sixty Four Character Field Limit"


def invalid_static_ip_examples():
    """Each is missing/malformed in a different field; /api/config requires
    all four (ip/dns/gateway/subnet) to parse as valid IPAddress when
    useDhcp=false, else 400 'invalid ip config'."""
    return [
        {"ip": "not-an-ip", "dns": "192.168.2.1", "gateway": "192.168.2.1", "subnet": "255.255.255.0"},
        {"ip": "192.168.2.154", "dns": "", "gateway": "192.168.2.1", "subnet": "255.255.255.0"},
        {"ip": "192.168.2.154", "dns": "192.168.2.1", "gateway": "999.999.999.999", "subnet": "255.255.255.0"},
        {"ip": "192.168.2.154", "dns": "192.168.2.1", "gateway": "192.168.2.1", "subnet": "not-a-mask"},
    ]


def clear_wifi(http, confirm=True):
    fields = {}
    if confirm:
        fields["confirm"] = "1"
    return http.post("/api/clearwifi", fields)


def reboot(http):
    return http.post("/api/reboot", {})


def wifi_rescan(http):
    return http.post("/api/wifi/rescan", {})


def set_labels(http, relay_count, relay_specs):
    fields = {}
    for spec in relay_specs[:relay_count]:
        entry = spec.to_template_entry()
        fields[f"relay{spec.id}_on"] = entry["o"]
        fields[f"relay{spec.id}_off"] = entry["f"]
        fields[f"relay{spec.id}_mode"] = entry["m"]
        fields[f"relay{spec.id}_group"] = str(entry["g"])
        fields[f"relay{spec.id}_pulse"] = str(entry["p"])
    return http.post("/api/labels", fields)
