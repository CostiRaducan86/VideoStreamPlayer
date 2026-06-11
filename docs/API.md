# VilsSharpX Automation REST API

## Overview

VilsSharpX exposes an in-process REST API for programmatic control of the application.
The API runs on an embedded Kestrel server and supports both loopback-only (localhost) and
remote access with full authentication and transport encryption.

**Base URL**: `http://127.0.0.1:8420` (default) or `https://<bind-address>:<port>` when HTTPS is enabled.

## Quick Start

### 1. Enable Remote Access (optional)

By default the API only listens on `127.0.0.1` and requires no authentication.
To allow remote clients:

1. Open **Configuration > API Configuration** in VilsSharpX.
2. Check **Allow Remote Access**.
3. Optionally check **Enable HTTPS (TLS)** for encrypted transport.
4. Set the **Bind Address** to `0.0.0.0` (all interfaces) or a specific NIC IP.
5. Generate an **API Key** (click "Generate Key").
6. Add allowed client IPs to the **Allowed CIDR Ranges** list.
7. Click **OK** and restart the application.

### 2. Test Connectivity

```powershell
# LOCAL — always works (HTTP loopback, no auth needed, even with HTTPS enabled)
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/health"

# REMOTE — requires X-Api-Key header + HTTPS
$headers = @{ "X-Api-Key" = "YOUR_TOKEN_HERE" }
Invoke-RestMethod -Uri "https://10.168.50.102:8420/api/v1/health/details" -Headers $headers
```

## Authentication and Security

### Transport Layer

| Mode            | Binding             | Encryption                                           |
| --------------- | ------------------- | ---------------------------------------------------- |
| Loopback        | `127.0.0.1:8420`    | None (HTTP) — safe, traffic never leaves machine     |
| Remote HTTP     | `<bind-ip>:8420`    | None — **not recommended** for production            |
| Remote HTTPS    | `<bind-ip>:8420`    | TLS 1.2/1.3 with self-signed RSA-2048 certificate    |

### Dual-Bind Architecture (HTTPS mode)

When HTTPS is enabled and the bind address is a specific remote IP (e.g. `10.168.50.102`),
Kestrel binds **two** listeners on the same port:

- **HTTPS** on the configured IP (remote clients, requires API key)
- **HTTP** on `127.0.0.1` (local tools, no certificate needed, no auth required)

This means `http://127.0.0.1:8420` always works locally regardless of HTTPS settings.

### HTTPS Certificate

When HTTPS is enabled, VilsSharpX automatically generates a self-signed X.509 certificate:

- **Storage**: `%APPDATA%\VilsSharpX\vilssharpx_api.pfx`
- **Subject**: `CN=VilsSharpX Automation API`
- **Key**: RSA 2048-bit
- **Validity**: 5 years (auto-regenerated 30 days before expiry)
- **SANs**: `localhost`, `127.0.0.1`, `::1`, `*.local`, plus the configured bind IP

The bind IP address is automatically added to the certificate SAN, so remote clients
connecting to `https://<bind-ip>:8420` can validate the certificate without bypass flags.

### Authentication Flow

```text
1. Is source IP == 127.0.0.1 (loopback)?
   YES -> Allow (no header required)

2. Is source IP in the CIDR allowlist?
   NO  -> Reject 401

3. Does request contain valid X-Api-Key header?
   NO  -> Reject 401
   YES -> Allow
```

### X-Api-Key Header

Remote requests must include the `X-Api-Key` HTTP header:

```text
X-Api-Key: wN3xK7p2mQ9... (base64url token, ~43 chars)
```

The key is generated cryptographically (32 bytes, base64url) and stored encrypted via
Windows DPAPI on the host machine.

### IP Allowlist

The allowlist supports three formats:

| Format       | Example              | Description                     |
| ------------ | -------------------- | ------------------------------- |
| Single IP    | `10.168.55.149`      | Exactly one host                |
| IP Range     | `10.168.55.149-155`  | Last octet range (inclusive)    |
| CIDR         | `10.168.50.0/24`     | Subnet notation                 |

At runtime, single IPs are expanded to `/32` and ranges are expanded to individual `/32` entries.
An empty allowlist means **all remote IPs are allowed** (only API key is checked).

## Endpoints

All endpoints are prefixed with `/api/v1/`.

### GET /api/v1/health

Lightweight health probe. No authentication required (even for remote).

**Response** (200 OK):

```json
{
  "ok": true,
  "utc": "2026-06-11T14:30:00.000Z"
}
```

### GET /api/v1/health/details

Detailed host status including security configuration. Requires authentication for remote clients.

**Response** (200 OK):

```json
{
  "ok": true,
  "utc": "2026-06-11T14:30:00.000Z",
  "host": {
    "bindAddress": "10.168.50.102",
    "port": 8420,
    "baseUrl": "https://10.168.50.102:8420",
    "isLoopbackBinding": false,
    "remoteRequestsEnabled": true,
    "apiKeyRequiredForRemote": true,
    "allowlistEnabled": true,
    "allowlistCidrs": ["10.168.55.140/32", "10.168.50.149/32"]
  },
  "request": {
    "remoteIp": "10.168.55.140",
    "isLoopback": false,
    "remoteIpAllowed": true,
    "hasApiKeyHeader": true
  }
}
```

### GET /api/v1/commands

Lists all supported command names. Requires authentication for remote clients.

**Response** (200 OK):

```json
{
  "commands": [
    "Ping",
    "StartSimulation",
    "StopSimulation",
    "PauseSimulation",
    "ResumeSimulation",
    "SetComparisonSettings",
    "GetComparisonStats",
    "GetFrameSnapshot"
  ]
}
```

### POST /api/v1/command

Executes a command. All commands use the same envelope format.

**Request Body**:

```json
{
  "command": "CommandName",
  "requestId": "optional-correlation-id",
  "payload": {}
}
```

**Success Response** (200 OK):

```json
{
  "requestId": "optional-correlation-id",
  "ok": true,
  "command": "CommandName",
  "data": {},
  "error": null
}
```

**Error Response** (4xx/5xx):

```json
{
  "requestId": "optional-correlation-id",
  "ok": false,
  "command": "CommandName",
  "data": null,
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable description",
    "details": null
  }
}
```

## Commands Reference

### Ping

Tests connectivity and returns the current application state.

```json
{ "command": "Ping" }
```

**Response data**:

```json
{
  "pong": true,
  "utc": "2026-06-11T14:30:00.000Z",
  "isRunning": true,
  "isPaused": false
}
```

### StartSimulation

Starts the capture/playback pipeline. If already running and paused, resumes instead.

```json
{
  "command": "StartSimulation",
  "payload": { "fps": 100 }
}
```

| Field | Type | Default | Description                     |
| ----- | ---- | ------- | ------------------------------- |
| `fps` | int  | 100     | Target frame rate (clamped >=1) |

**Response data**:

```json
{ "started": true, "fps": 100 }
```

### StopSimulation

Stops the active capture/playback session.

```json
{ "command": "StopSimulation" }
```

**Response data**:

```json
{ "stopped": true }
```

### PauseSimulation

Pauses the active session (freezes all panes).

```json
{ "command": "PauseSimulation" }
```

**Response data**:

```json
{ "paused": true }
```

### ResumeSimulation

Resumes a paused session.

```json
{ "command": "ResumeSimulation" }
```

**Response data**:

```json
{ "resumed": true }
```

### SetComparisonSettings

Updates comparison parameters. Only provided fields are applied; omitted fields remain unchanged.

```json
{
  "command": "SetComparisonSettings",
  "payload": {
    "mode": 0,
    "deadband": 5,
    "bDelta": 0
  }
}
```

| Field | Type | Range | Description |
| --- | --- | --- | --- |
| `mode` | int | 0-2 | 0=LVDS-AVTP, 1=LSM-LVDS, 2=LSM-AVTP |
| `deadband` | int | 0-255 | Pixel deviation below this is considered "green" |
| `bDelta` | int | any | Value offset applied to B frame before comparison |

**Response data**:

```json
{ "updated": true, "mode": 0, "deadband": 5, "bDelta": 0 }
```

### GetComparisonStats

Returns the most recent comparison statistics (updated each render cycle).

```json
{ "command": "GetComparisonStats" }
```

**Response data**:

```json
{
  "max_positive_dev": 12,
  "max_negative_dev": -3,
  "average_pixels_dev": 2.7,
  "total_pixels_dev": 1450,
  "total_dark_pixels": 0
}
```

| Field                | Description                                     |
| -------------------- | ----------------------------------------------- |
| `max_positive_dev`   | Maximum B>A deviation (positive)                |
| `max_negative_dev`   | Maximum B<A deviation (negative)                |
| `average_pixels_dev` | Mean absolute deviation across all pixels       |
| `total_pixels_dev`   | Number of pixels exceeding the deadband         |
| `total_dark_pixels`  | Pixels where A>0 but B==0 (dark pixel defects)  |

### GetFrameSnapshot

Captures a PNG snapshot of a specific pane.

```json
{
  "command": "GetFrameSnapshot",
  "payload": { "pane": "D" }
}
```

| Field | Type | Values | Description |
| --- | --- | --- | --- |
| `pane` | string | `"A"`, `"B"`, `"D"` | Which visualization pane to capture |

**Response data**:

```json
{
  "pane": "D",
  "format": "png-base64",
  "image": "iVBORw0KGgoAAAANSU..."
}
```

The `image` field is a base64-encoded PNG (Gray8 for A/B, BGR24 for D).

## Error Codes

| Code | HTTP Status | Meaning |
| --- | --- | --- |
| `UNAUTHORIZED` | 401 | Missing/invalid API key or IP not in allowlist |
| `BAD_REQUEST` | 400 | Invalid parameters or missing required fields |
| `BAD_JSON` | 400 | Request body is not valid JSON |
| `UNKNOWN_COMMAND` | 404 | Command name not recognized |
| `NOT_IMPLEMENTED` | 501 | Command exists but is not yet implemented |
| `INTERNAL_ERROR` | 500 | Unexpected server-side error |

## PowerShell Examples

### Local (always works, no auth, no cert)

```powershell
# Health check
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/health"

# Start simulation
$body = '{"command":"StartSimulation","payload":{"fps":100}}'
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/command" -Method Post -Body $body -ContentType "application/json"

# Get comparison stats
$body = '{"command":"GetComparisonStats"}'
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/command" -Method Post -Body $body -ContentType "application/json"

# Stop simulation
$body = '{"command":"StopSimulation","payload":{}}'
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/command" -Method Post -Body $body -ContentType "application/json"
```

### Remote (HTTPS + API key)

```powershell
$headers = @{ "X-Api-Key" = "iDFrPdQ5AHYn4Xpbe21t-tZShV8QaIkCrd6GtHSFCwo" }

# Health details
Invoke-RestMethod -Uri "https://10.168.50.102:8420/api/v1/health/details" -Headers $headers

# Ping
$body = '{"command":"Ping"}'
Invoke-RestMethod -Uri "https://10.168.50.102:8420/api/v1/command" -Method Post -Body $body -ContentType "application/json" -Headers $headers
```

## Python SDK Example

```python
import requests
import base64
from pathlib import Path


class VilsSharpXClient:
    """Minimal Python client for the VilsSharpX Automation API."""

    def __init__(self, base_url="http://127.0.0.1:8420", api_key=None, verify_ssl=True):
        self.base_url = base_url.rstrip("/")
        self.session = requests.Session()
        self.session.verify = verify_ssl
        if api_key:
            self.session.headers["X-Api-Key"] = api_key

    def health(self):
        """Check API availability."""
        r = self.session.get(f"{self.base_url}/api/v1/health")
        r.raise_for_status()
        return r.json()

    def command(self, cmd, payload=None, request_id=None):
        """Execute a command and return the response data."""
        body = {"command": cmd}
        if payload:
            body["payload"] = payload
        if request_id:
            body["requestId"] = request_id

        r = self.session.post(f"{self.base_url}/api/v1/command", json=body)
        resp = r.json()
        if not resp.get("ok"):
            raise RuntimeError(f"API error: {resp.get('error', {}).get('message', 'unknown')}")
        return resp.get("data")

    def ping(self):
        return self.command("Ping")

    def start(self, fps=100):
        return self.command("StartSimulation", {"fps": fps})

    def stop(self):
        return self.command("StopSimulation")

    def pause(self):
        return self.command("PauseSimulation")

    def resume(self):
        return self.command("ResumeSimulation")

    def set_comparison(self, mode=None, deadband=None, b_delta=None):
        payload = {}
        if mode is not None:
            payload["mode"] = mode
        if deadband is not None:
            payload["deadband"] = deadband
        if b_delta is not None:
            payload["bDelta"] = b_delta
        return self.command("SetComparisonSettings", payload)

    def get_stats(self):
        return self.command("GetComparisonStats")

    def get_snapshot(self, pane="D", save_path=None):
        data = self.command("GetFrameSnapshot", {"pane": pane})
        png_bytes = base64.b64decode(data["image"])
        if save_path:
            Path(save_path).write_bytes(png_bytes)
        return png_bytes


if __name__ == "__main__":
    # Local (no auth needed)
    client = VilsSharpXClient()
    print(client.health())
    print(client.ping())

    # Remote with HTTPS + API key
    # client = VilsSharpXClient(
    #     base_url="https://10.168.50.102:8420",
    #     api_key="iDFrPdQ5AHYn4Xpbe21t-tZShV8QaIkCrd6GtHSFCwo",
    #     verify_ssl=True
    # )
```

## Configuration Reference

Settings are stored in `%APPDATA%\VilsSharpX\settings.json`:

```json
{
  "ApiAllowRemote": true,
  "ApiEnableHttps": true,
  "ApiBindAddress": "10.168.50.102",
  "ApiPort": 8420,
  "ApiKeyProtected": "...(DPAPI encrypted)...",
  "ApiAllowedCidrs": ["10.168.55.140-150", "10.168.50.149"]
}
```

| Field             | Default      | Description                                         |
| ----------------- | ------------ | --------------------------------------------------- |
| `ApiAllowRemote`  | `false`      | Enable remote access (binds to configured address)  |
| `ApiEnableHttps`  | `false`      | Enable TLS transport with self-signed certificate   |
| `ApiBindAddress`  | `127.0.0.1`  | Network interface to bind to                        |
| `ApiPort`         | `8420`       | TCP port                                            |
| `ApiKeyProtected` | `""`         | DPAPI-encrypted API key                             |
| `ApiAllowedCidrs` | `[]`         | IP allowlist (single IPs, ranges, or CIDR notation) |

## Architecture

```text
VilsSharpX.exe
  |
  +-- Kestrel (HTTP on 127.0.0.1 + HTTPS on bind-ip)
  |     |
  |     +-- /api/v1/health          (no auth)
  |     +-- /api/v1/health/details  (auth required for remote)
  |     +-- /api/v1/commands        (auth required for remote)
  |     +-- /api/v1/command         (auth required for remote)
  |           |
  |           v
  |     CommandRouter.cs (dispatch + validation)
  |           |
  |           v
  |     IGuiAutomationBridge (WPF MainWindow)
  |
  +-- Security Layer
        - IP allowlist (CidrRange)
        - X-Api-Key validation
        - TLS (SelfSignedCertificate.cs)
        - DPAPI key storage
```

## Versioning

The API follows `/api/v1/` prefix versioning. Breaking changes will increment the version
(e.g., `/api/v2/`). The current version is **v1**.
