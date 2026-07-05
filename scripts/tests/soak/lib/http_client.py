"""HTTP client for the relay board's REST API.

Mirrors the retry conventions of scripts/tests/Invoke-BoardFunctionalHarness.ps1
(Invoke-ApiWithRetry / Is-TransientApiFailure) so results stay comparable across
the PowerShell smoke harness and this suite. Every call records latency.
"""
import http.cookiejar
import json
import time
import urllib.error
import urllib.parse
import urllib.request

TRANSIENT_ERROR_SUBSTRINGS = (
    "temp_open_failed",
    "storage busy",
    "storage_write_lock",
)


class ApiResult:
    def __init__(self, status, body, latency_ms, error=None):
        self.status = status
        self.body = body
        self.latency_ms = latency_ms
        self.error = error

    @property
    def ok(self):
        return self.error is None and self.status is not None and 200 <= self.status < 300

    def json(self):
        if not self.body:
            return None
        try:
            return json.loads(self.body)
        except (ValueError, TypeError):
            return None

    def __repr__(self):
        return f"ApiResult(status={self.status}, latency_ms={self.latency_ms:.1f}, error={self.error!r})"


class HttpClient:
    """Cookie-jar-aware client for one board's base URL (http://<ip>)."""

    def __init__(self, host, timeout=8):
        self.host = host
        self.base_url = f"http://{host}"
        self.timeout = timeout
        self._cookie_jar = http.cookiejar.CookieJar()
        self._opener = urllib.request.build_opener(
            urllib.request.HTTPCookieProcessor(self._cookie_jar)
        )

    def _is_transient(self, status, body, exc):
        if exc is not None and status is None:
            return True  # connection-level failure (timeout, refused, etc.)
        if status in (408, 429) or (status is not None and status >= 500):
            return True
        if body:
            lower = body.lower()
            if any(s in lower for s in TRANSIENT_ERROR_SUBSTRINGS):
                return True
        return False

    def _request_once(self, method, path, fields=None, timeout=None):
        url = self.base_url + path
        data = None
        req_timeout = timeout or self.timeout
        headers = {}
        if fields is not None and method in ("POST", "PUT"):
            data = urllib.parse.urlencode(fields).encode("utf-8")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        t0 = time.perf_counter()
        try:
            with self._opener.open(req, timeout=req_timeout) as resp:
                body = resp.read().decode("utf-8", errors="replace")
                latency_ms = (time.perf_counter() - t0) * 1000
                return ApiResult(resp.status, body, latency_ms)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")
            latency_ms = (time.perf_counter() - t0) * 1000
            return ApiResult(e.code, body, latency_ms)
        except Exception as e:
            latency_ms = (time.perf_counter() - t0) * 1000
            return ApiResult(None, None, latency_ms, error=str(e))

    def request(self, method, path, fields=None, timeout=None, max_attempts=3, retry_delay_s=0.2):
        last = None
        for attempt in range(1, max_attempts + 1):
            result = self._request_once(method, path, fields=fields, timeout=timeout)
            last = result
            if result.ok or not self._is_transient(result.status, result.body, result.error):
                return result
            if attempt < max_attempts:
                time.sleep(retry_delay_s)
        return last

    def get(self, path, **kw):
        return self.request("GET", path, **kw)

    def post(self, path, fields, **kw):
        return self.request("POST", path, fields=fields, **kw)

    def delete(self, path, **kw):
        return self.request("DELETE", path, **kw)

    def get_raw(self, path, timeout=None):
        """Fetch a raw (non-JSON) resource, e.g. a static template/board file
        or an HTML page/asset. Returns (status, bytes, latency_ms)."""
        url = self.base_url + path
        t0 = time.perf_counter()
        try:
            with self._opener.open(url, timeout=timeout or self.timeout) as resp:
                body = resp.read()
                latency_ms = (time.perf_counter() - t0) * 1000
                return resp.status, body, latency_ms
        except urllib.error.HTTPError as e:
            latency_ms = (time.perf_counter() - t0) * 1000
            return e.code, e.read(), latency_ms
        except Exception as e:
            latency_ms = (time.perf_counter() - t0) * 1000
            return None, None, latency_ms
