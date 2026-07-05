"""Relay combination generation for the soak test.

Two responsibilities:
1. build_test_template_spec(relay_count) -- design a representative template
   covering every mode (Latched/Interlocked/Pulsed) across multiple groups,
   sized to whatever the board's actual relay count is. This is content, not
   behavior: it becomes the JSON body uploaded via templates_api.create_content.
2. iter_combinations(spec, duration_s) -- a generator that continuously
   yields (relay_id, action) pairs across at least duration_s seconds,
   respecting group exclusivity (only one member of a non-zero group can be
   "on" at a time -- true for Interlocked, and also true for Latched/Pulsed
   buttons that have a group assigned, per the button-mode rules in
   Current Status.md). The generator doesn't compute the resulting on-set
   itself; the firmware's own interlock/group logic resolves that when the
   caller sends the toggle over WS -- the caller is expected to verify actual
   state via ws_client.get_home() after each action.
"""
import itertools
import time

MODE_LATCHED = "L"
MODE_INTERLOCKED = "I"
MODE_PULSED = "P"


class RelaySpec:
    def __init__(self, relay_id, mode, group, pulse_timeout=0, on_label=None, off_label=None):
        self.id = relay_id
        self.mode = mode
        self.group = group
        self.pulse_timeout = pulse_timeout
        self.on_label = on_label or f"Soak Test Relay {relay_id} On"
        self.off_label = off_label or f"Soak Test Relay {relay_id} Off"

    def to_template_entry(self):
        return {
            "o": self.on_label,
            "f": self.off_label,
            "m": self.mode,
            "g": self.group,
            "p": self.pulse_timeout if self.mode == MODE_PULSED else 0,
        }


def build_test_template_spec(relay_count):
    """Distribute relay_count relays across L/I/P modes and several groups so
    every mode and every group-exclusivity interaction gets exercised,
    regardless of whether relay_count is 8 or 16.

    Layout (proportional, minimum 1 relay per category when relay_count allows):
      - ~40% Interlocked -- split into the MAXIMUM number of distinct groups
        possible (pairs of 2, so every group still shows real cross-member
        exclusivity; an odd leftover becomes its own singleton group), rather
        than just 2 broad groups. More, smaller groups exercise more distinct
        group-boundary code paths (e.g. verifying group A is undisturbed when
        group B's member toggles) than fewer, larger ones.
      - ~30% Pulsed -- same max-groups pairing, alternating short timeouts
        (1-2s) so auto-off can be verified quickly within the soak window.
      - ~30% Latched: half split into max-groups pairs (mutual exclusion like
        Interlocked), half left ungrouped/group 0 (fully independent -- this
        is itself a distinct, valid Latched configuration worth covering,
        since Latched buttons may *optionally* have a group).
    """
    specs = []
    n = relay_count
    n_interlocked = max(2, round(n * 0.4))
    n_pulsed = max(2, round(n * 0.3))
    n_interlocked = min(n_interlocked, n)
    n_pulsed = min(n_pulsed, n - n_interlocked)
    n_latched = n - n_interlocked - n_pulsed

    relay_id = 1
    interlocked_groups = _max_groups(n_interlocked, group_id_start=1)
    for group in interlocked_groups:
        specs.append(RelaySpec(relay_id, MODE_INTERLOCKED, group))
        relay_id += 1

    pulsed_groups = _max_groups(n_pulsed, group_id_start=100)
    for i, group in enumerate(pulsed_groups):
        timeout = 1 if i % 2 == 0 else 2
        specs.append(RelaySpec(relay_id, MODE_PULSED, group, pulse_timeout=timeout))
        relay_id += 1

    # Latched: first half split into max-groups pairs (group ids 200+),
    # remainder ungrouped (group 0).
    n_latched_grouped = n_latched // 2
    latched_groups = _max_groups(n_latched_grouped, group_id_start=200)
    for i in range(n_latched):
        group = latched_groups[i] if i < n_latched_grouped else 0
        specs.append(RelaySpec(relay_id, MODE_LATCHED, group))
        relay_id += 1

    assert len(specs) == relay_count, f"expected {relay_count} specs, built {len(specs)}"
    return specs


def _max_groups(count, group_id_start):
    """Assign `count` items to the maximum number of distinct groups: pairs of
    2 wherever possible (so exclusivity is actually demonstrable -- toggling
    one member has another to turn off), with a single leftover item forming
    its own singleton group rather than being folded into an existing pair."""
    group_ids = []
    gid = group_id_start
    i = 0
    while i < count:
        remaining = count - i
        if remaining == 1:
            group_ids.append(gid)
            i += 1
        else:
            group_ids.extend([gid, gid])
            i += 2
        gid += 1
    return group_ids


def _groups(specs):
    grouped = {}
    ungrouped = []
    for s in specs:
        if s.group and s.group > 0:
            grouped.setdefault(s.group, []).append(s)
        else:
            ungrouped.append(s)
    return grouped, ungrouped


def iter_combinations(specs, duration_s=60):
    """Yield (relay_id, note) pairs for at least duration_s seconds of varied
    combinations. Pacing comes entirely from the caller's own per-yield work
    (a real WS round-trip via ws_client.toggle() takes ~150ms+) -- this
    generator itself never sleeps, it just checks the deadline between yields.
    `note` is a short string describing why (for logging), e.g.
    "group 1 member 2/3" or "pulsed, expect auto-off in 2s" or "ungrouped toggle".

    Guarantees every relay in `specs` is toggled at least once (in the first
    full pass), then continues cycling with shifting offsets to keep
    generating different combinations for the remaining time budget.
    """
    grouped, ungrouped = _groups(specs)
    group_ids = sorted(grouped.keys())
    start = time.time()
    pass_num = 0

    while time.time() - start < duration_s:
        # One "pass": cycle every group through each of its members once,
        # and toggle every ungrouped relay on then off -- offset by pass_num
        # so which member leads varies between passes.
        for gid in group_ids:
            members = grouped[gid]
            order = members[pass_num % len(members):] + members[: pass_num % len(members)]
            for spec in order:
                if spec.mode == MODE_PULSED:
                    note = f"group {gid} pulsed relay {spec.id}, expect auto-off in {spec.pulse_timeout}s"
                else:
                    note = f"group {gid} member {spec.id} ({spec.mode})"
                yield spec.id, note
                if time.time() - start >= duration_s:
                    return

        offset = pass_num % max(1, len(ungrouped))
        rotated = ungrouped[offset:] + ungrouped[:offset]
        for spec in rotated:
            yield spec.id, f"ungrouped {spec.mode} relay {spec.id} on"
            if time.time() - start >= duration_s:
                return
        for spec in rotated:
            yield spec.id, f"ungrouped {spec.mode} relay {spec.id} off"
            if time.time() - start >= duration_s:
                return

        pass_num += 1


def max_pulse_timeout(specs):
    pulsed = [s.pulse_timeout for s in specs if s.mode == MODE_PULSED]
    return max(pulsed) if pulsed else 0
