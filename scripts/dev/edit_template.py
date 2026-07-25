"""Edit relay button fields in an existing data/templates/*.json file, applying
the exact same validation/clamping the firmware applies when it writes a
template (see writeTemplateJson in src/template_routes.cpp), so the file on
disk ends up byte-for-byte what the board itself would have written.

Usage:
    python scripts/dev/edit_template.py <file> --relay N FIELD VALUE [--relay N FIELD VALUE ...]

FIELD is one of: o (on label), f (off label), m (mode: L/I/P), g (group 0-255),
p (pulse timeout seconds, only meaningful when mode=P).

Example:
    python scripts/dev/edit_template.py data/templates/antenna-disconnection.json \\
        --relay 1 o "Pump On" --relay 1 f "Pump Off" --relay 3 m P --relay 3 p 30
"""
import argparse
import json
import os
import sys

# Mirrors src/route_data.h
K_MAX_PULSE_TIMEOUT_SECONDS = 60
K_DEFAULT_PULSE_TIMEOUT_SECONDS = 1
K_MAX_TEMPLATE_TITLE_LENGTH = 40
K_MAX_TEMPLATE_FILENAME_LENGTH = 26
VALID_RELAY_COUNTS = (8, 16)


def sanitize_template_slug(title: str) -> str:
    """Mirrors sanitizeTemplateSlug in src/template_routes.cpp exactly."""
    out = []
    for ch in title:
        if len(out) >= K_MAX_TEMPLATE_FILENAME_LENGTH:
            break
        if ch.isascii() and ch.isalnum():
            out.append(ch.lower())
        elif ch in (' ', '-', '_') and len(out) > 0 and out[-1] != '-':
            out.append('-')
    while out and out[-1] == '-':
        out.pop()
    if not out:
        return 'template'
    return ''.join(out)


def write_escaped_json_string(value: str) -> str:
    """Mirrors writeEscapedJsonString in src/template_routes.cpp exactly."""
    out = ['"']
    for ch in value:
        if ch == '"':
            out.append('\\"')
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '\b':
            out.append('\\b')
        elif ch == '\f':
            out.append('\\f')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\r':
            out.append('\\r')
        elif ch == '\t':
            out.append('\\t')
        elif ord(ch) < 0x20:
            out.append('\\u%04X' % ord(ch))
        else:
            out.append(ch)
    out.append('"')
    return ''.join(out)


def clamp_label(doc, index, field, raw_value, warnings):
    """Applies the same clamp/validate rules writeTemplateJson uses per-field."""
    labels = doc['l']
    label = labels[index]

    if field == 'o':
        label['o'] = raw_value.strip()
    elif field == 'f':
        label['f'] = raw_value.strip()
    elif field == 'm':
        mode = raw_value.strip()
        if mode not in ('I', 'P'):
            if mode != 'L':
                warnings.append(
                    f"relay {index + 1}: mode {raw_value!r} is not I/P/L, "
                    f"hardware forces it to \"L\""
                )
            mode = 'L'
        label['m'] = mode
    elif field == 'g':
        try:
            g = int(raw_value)
        except ValueError:
            warnings.append(f"relay {index + 1}: group {raw_value!r} is not an integer, using 0")
            g = 0
        wrapped = g & 0xFF
        if wrapped != g:
            warnings.append(
                f"relay {index + 1}: group {g} does not fit uint8_t, "
                f"hardware wraps it to {wrapped}"
            )
        label['g'] = wrapped
    elif field == 'p':
        try:
            p = int(raw_value)
        except ValueError:
            warnings.append(f"relay {index + 1}: pulse {raw_value!r} is not an integer, using 0")
            p = 0
        label['p'] = p & 0xFF
    else:
        raise ValueError(f"unknown field {field!r} (expected one of: o f m g p)")


def clamp_title(raw_title, warnings):
    title = str(raw_title).strip()
    if len(title) == 0:
        warnings.append('title is empty — hardware defaults this to "Imported Template" on import')
        title = 'Imported Template'
    if len(title) > K_MAX_TEMPLATE_TITLE_LENGTH:
        warnings.append(f'title is {len(title)} chars, hardware truncates it to {K_MAX_TEMPLATE_TITLE_LENGTH}')
        title = title[:K_MAX_TEMPLATE_TITLE_LENGTH]
    return title


def apply_pulse_mode_rule(doc, warnings):
    """Mirrors the mode/pulseTimeout interaction in writeTemplateJson: pulseTimeout
    is only kept when mode == P, and clamped to 1..60, else forced to 0."""
    for i, label in enumerate(doc['l']):
        if label['m'] == 'P':
            p = label.get('p', 0)
            if p == 0 or p > K_MAX_PULSE_TIMEOUT_SECONDS:
                if p != 0:
                    warnings.append(
                        f"relay {i + 1}: pulse {p}s exceeds max "
                        f"{K_MAX_PULSE_TIMEOUT_SECONDS}s, hardware clamps it to "
                        f"{K_DEFAULT_PULSE_TIMEOUT_SECONDS}s"
                    )
                label['p'] = K_DEFAULT_PULSE_TIMEOUT_SECONDS
        else:
            if label.get('p', 0) != 0:
                warnings.append(
                    f"relay {i + 1}: mode is not P, hardware discards pulse "
                    f"and stores p=0"
                )
            label['p'] = 0


def serialize_template(doc) -> str:
    """Same field order/escaping as writeTemplateJson in src/template_routes.cpp,
    but kept on one compact line to match the existing on-disk convention in
    data/templates/*.json (whitespace is irrelevant to the firmware's parser
    either way — deserializeJson doesn't care)."""
    parts = [f'"t":{write_escaped_json_string(doc["t"])}', f'"n":{doc["n"]}']
    label_parts = []
    for label in doc['l']:
        on = write_escaped_json_string(label.get('o', ''))
        off = write_escaped_json_string(label.get('f', ''))
        mode = write_escaped_json_string(label.get('m', 'L'))
        group = label.get('g', 0)
        pulse = label.get('p', 0)
        label_parts.append(f'{{"o":{on},"f":{off},"m":{mode},"g":{group},"p":{pulse}}}')
    parts.append('"l":[' + ','.join(label_parts) + ']')
    return '{' + ','.join(parts) + '}\n'


def load_template(path, warnings):
    with open(path, 'r', encoding='utf-8') as f:
        doc = json.load(f)

    labels = doc.get('l')
    if not isinstance(labels, list) or len(labels) == 0:
        raise ValueError('template has no "l" (labels) array — hardware would reject this as "labels missing"')

    rc = doc.get('n', len(labels))
    if rc <= 0 or rc > 16:
        raise ValueError(f'"n"={rc} is out of range 1-16 — hardware would reject this as "invalid relay count"')
    if rc != len(labels):
        raise ValueError(
            f'"n"={rc} does not match {len(labels)} entries in "l" — '
            f'hardware would reject this as "relay count does not match active board" '
            f'(or, on import, as an internal mismatch)'
        )
    if rc not in VALID_RELAY_COUNTS:
        warnings.append(f'"n"={rc} is not a real board variant ({VALID_RELAY_COUNTS}) — no physical board could ever select this template')

    doc['t'] = clamp_title(doc.get('t', ''), warnings)
    doc['n'] = rc

    for label in labels:
        if 'm' not in label or label['m'] not in ('I', 'P'):
            label['m'] = 'L' if label.get('m', 'L') not in ('I', 'P') else label['m']
        label.setdefault('g', 0)
        label.setdefault('p', 0)

    return doc


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('file', help='path to an existing data/templates/*.json file')
    parser.add_argument(
        '--relay', nargs=3, action='append', default=[], metavar=('N', 'FIELD', 'VALUE'),
        help='1-based relay index, field (o/f/m/g/p), and new value; repeatable'
    )
    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print(f'error: {args.file} not found', file=sys.stderr)
        sys.exit(1)

    warnings = []
    try:
        doc = load_template(args.file, warnings)
    except ValueError as exc:
        print(f'error: {exc}', file=sys.stderr)
        sys.exit(1)

    labels = doc['l']
    for n_str, field, value in args.relay:
        try:
            n = int(n_str)
        except ValueError:
            print(f'error: relay index {n_str!r} is not an integer', file=sys.stderr)
            sys.exit(1)
        if n < 1 or n > len(labels):
            print(f'error: relay {n} out of range (this template has {len(labels)} relays)', file=sys.stderr)
            sys.exit(1)
        try:
            clamp_label(doc, n - 1, field, value, warnings)
        except ValueError as exc:
            print(f'error: {exc}', file=sys.stderr)
            sys.exit(1)

    apply_pulse_mode_rule(doc, warnings)

    for w in warnings:
        print(f'warning: {w}', file=sys.stderr)

    with open(args.file, 'w', encoding='utf-8', newline='\n') as f:
        f.write(serialize_template(doc))

    print(f'wrote {args.file}')


if __name__ == '__main__':
    main()
