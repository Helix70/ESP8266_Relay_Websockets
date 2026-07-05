"""Wrappers for every /api/templates action, per the route mapping researched
in src/template_routes.cpp. See src/CLAUDE.md and Current Status.md for the
session notes this suite grew out of.

Route reality (important for the coverage pass, not just happy-path use):
  - POST /api/templates is the one real, live handler; `action` (form field)
    dispatches to save/upload/setactive/rename/delete internally.
  - POST /api/templates/select and POST /api/templates/upload are dead code:
    confirmed via direct curl this session that the base /api/templates POST
    handler (registered first, using the library's default BackwardCompatible
    URI matcher `^{uri}(/.*)?$`) silently intercepts any POST under
    /api/templates/* before these two ever see the request -- POST
    /api/templates/select?filename=x returns the *default/save* branch's
    "title required" error, not anything from the dedicated select handler.
    No live impact (the real UI always posts action=setactive/upload to the
    base endpoint), but exercised here for coverage of the actual runtime
    behavior, which is "silently shadowed", not "separately functional".
  - The default/fallback action ("save", i.e. no `action` or an unrecognized
    one) always derives the filename from `title` server-side and ignores any
    `filename` field -- only the `action=upload` (content-based) path lets you
    pick an explicit filename.
"""
import json


def list_templates(http):
    return http.get("/api/templates")


def diagnostics(http):
    """GET /api/templates/diagnostics: selectedTemplate, templateCount,
    largestTemplateBytes, fsTotalBytes/fsUsedBytes/fsFreeBytes,
    lastWriteErrorReason/lastWriteErrorAtMs."""
    return http.get("/api/templates/diagnostics")


def create_form(http, title, relay_specs):
    """action=save (default) path: relayCount comes from the server's active
    relayCount, filename is always auto-derived from title (any filename
    field here is ignored server-side)."""
    fields = {"action": "save", "title": title}
    for spec in relay_specs:
        entry = spec.to_template_entry()
        fields[f"relay{spec.id}_on"] = entry["o"]
        fields[f"relay{spec.id}_off"] = entry["f"]
        fields[f"relay{spec.id}_mode"] = entry["m"]
        fields[f"relay{spec.id}_group"] = str(entry["g"])
        fields[f"relay{spec.id}_pulse"] = str(entry["p"])
    return http.post("/api/templates", fields)


def create_content(http, title, filename, relay_count, relay_specs):
    """action=upload, content-based path: lets us pick an explicit filename
    and a full JSON body -- used for the purpose-built L/I/P/group template."""
    doc = {
        "t": title,
        "n": relay_count,
        "l": [spec.to_template_entry() for spec in relay_specs],
    }
    fields = {
        "action": "upload",
        "title": title,
        "filename": filename,
        "content": json.dumps(doc),
    }
    return http.post("/api/templates", fields)


def create_content_bug_regression(http, title, filename, relay_count):
    """Reproduces data/relay-config.js's uploadSelectedTemplateFile() exactly:
    it sends `relayCount` instead of `n` and never sends `content`, so the
    server-side form-fields path always sees n=0 and 400s with
    'content required'. This is a documented, currently-unfixed UI bug (see
    Current Status.md) -- this call exists to assert it stays reproducible,
    not to work around it."""
    fields = {
        "action": "upload",
        "title": title,
        "filename": filename,
        "relayCount": str(relay_count),
    }
    return http.post("/api/templates", fields)


def setactive(http, filename):
    return http.post("/api/templates", {"action": "setactive", "filename": filename})


def rename(http, filename, new_title):
    return http.post("/api/templates", {"action": "rename", "filename": filename, "title": new_title})


def delete(http, filename):
    return http.delete(f"/api/templates?filename={filename}")


def download(http, filename):
    return http.get_raw(f"/templates/{filename}")


def hit_dead_select_route(http, filename):
    """Direct hit on the shadowed standalone route. Because the request is
    actually serviced by the base /api/templates POST handler's default
    ("save") branch -- not the dedicated select handler -- and no `title` is
    sent, this is expected to return 400 "title required", exactly as
    confirmed via curl this session. Asserting this stays true is the coverage
    goal here, not exercising a working "select" behavior."""
    return http.post("/api/templates/select", {"filename": filename})


def hit_dead_upload_route(http, title, filename, relay_count, relay_specs):
    """Direct hit on the shadowed standalone route. Because this is actually
    serviced by the base handler's default ("save") branch (no `action` field
    is sent), `content` and the explicit `filename` are silently ignored --
    it behaves like a plain save-with-empty-labels using a filename derived
    from `title`, NOT the filename/content passed here. Callers should assert
    the response's filename is the slugified title, not the filename param,
    to prove the shadowing rather than assuming this uploaded anything."""
    doc = {
        "t": title,
        "n": relay_count,
        "l": [spec.to_template_entry() for spec in relay_specs],
    }
    fields = {"title": title, "filename": filename, "content": json.dumps(doc)}
    return http.post("/api/templates/upload", fields)


# ---- Invalid-input coverage helpers --------------------------------------
# kMaxTemplateTitleLength=40, filenames must end .json, contain no /,\,..,
# and stay under kMaxTemplateFilenameLength+5=31 chars total (route_data.h).

def long_title_example():
    """Exceeds kMaxTemplateTitleLength (40) -- expected to be silently
    truncated to 40 chars server-side, not rejected."""
    return "Soak Test Extremely Long Template Title That Exceeds The Forty Character Limit By Quite A Lot"


def invalid_filename_examples():
    """Expected to be rejected (400 'invalid filename') by
    normalizeTemplateFilename: path traversal, path separators, no .json
    extension, and a too-long name."""
    return [
        "../escape.json",
        "sub/dir.json",
        "back\\slash.json",
        "no-json-extension",
        "x" * 40 + ".json",
    ]


def single_member_group_spec(relay_count):
    """A deliberately 'incorrect'-looking but technically valid configuration:
    an Interlocked relay that is the ONLY member of its group. The firmware
    doesn't reject this (a group of one is legal, just has no exclusivity
    partner) -- worth covering since it's an easy misconfiguration for a real
    user to create by accident (e.g. deleting/renumbering a group's other
    member and forgetting to update this one)."""
    from combos import RelaySpec, MODE_INTERLOCKED

    return [RelaySpec(1, MODE_INTERLOCKED, group=42)]
