"""Wrappers for /api/theme, per src/config_system_routes.cpp.

Validation rules (confirmed from source):
  - h: comma-separated hex tokens, chars limited to [0-9a-fA-F#,], and the
    comma COUNT must be exactly 6 or 8 (i.e. 7 or 9 colour values).
  - s: optional, must be all-lowercase a-z if present.
"""


def get(http):
    return http.get("/api/theme")


def set_theme(http, h, s):
    return http.post("/api/theme", {"h": h, "s": s})


def invalid_hex_examples():
    """A few deliberately-invalid `h` values for the negative-path coverage
    test: wrong comma count, and an illegal character."""
    return [
        "#112233,#445566",  # 1 comma -- not 6 or 8
        "#112233,#445566,#778899,#GGGGGG,#ddeeff,#001122,#334455",  # bad hex char 'G'
    ]


def invalid_style_examples():
    """Deliberately-invalid `s` values: uppercase and digits are rejected."""
    return ["Classic", "style1", "has space"]
