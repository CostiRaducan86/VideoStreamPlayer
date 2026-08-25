# VilsSharpX Python SDK (v1)

Thin Python client for the **localhost or LAN** REST automation API exposed by the
VilsSharpX WPF application. Standard library only — no `pip install` required.

## Transport

- Default local base URL: `http://127.0.0.1:8420`
- Single command endpoint: `POST /api/v1/command`
- Health probe: `GET /api/v1/health`
- Command catalog: `GET /api/v1/commands`
- For remote/LAN access, configure the WPF app settings file and send `X-Api-Key`.

Every command uses the same envelope:

```json
{ "requestId": "<uuid>", "command": "StartSimulation", "payload": { "fps": 100 } }
```

Every response uses the same shape:

```json
{ "requestId": "<uuid>", "ok": true, "command": "StartSimulation", "data": { "started": true, "fps": 100 }, "error": null }
```

## v1 commands

| Command | Payload | Returns (`data`) |
| --- | --- | --- |
| `Ping` | `{}` | `{ pong, utc, isRunning, isPaused }` |
| `StartSimulation` | `{ "fps": 100 }` | `{ started, fps }` |
| `StopSimulation` | `{}` | `{ stopped }` |
| `PauseSimulation` | `{}` | `{ paused }` |
| `ResumeSimulation` | `{}` | `{ resumed }` |
| `SetComparisonSettings` | `{ "mode": 0, "deadband": 5, "bDelta": 0 }` | `{ updated, mode, deadband, bDelta }` |
| `GetComparisonStats` | `{}` | `{ max_positive_dev, max_negative_dev, avg_dev, total_pixels_dev, dark_pixels }` |
| `GetFrameSnapshot` | `{ "pane": "D" }` | `{ pane, format, image (base64 PNG) }` |

## Usage

```python
from vilssharpx import VilsClient

client = VilsClient()  # http://127.0.0.1:8420
client.start_simulation(fps=100)
client.set_comparison_settings(mode=0, deadband=5, b_delta=0)

stats = client.get_comparison_stats()
print(stats.total_pixels_dev)

client.save_frame_snapshot("paneD.png", pane="D")
client.stop_simulation()
```

## Remote / LAN access (v1.1)

Remote access is **disabled by default**. To allow commands from another PC, edit:

`%APPDATA%\VilsSharpX\settings.json`

Example:

```json
{
  "ApiAllowRemote": true,
  "ApiBindAddress": "0.0.0.0",
  "ApiPort": 8420,
  "ApiKey": "change-me-lab-token"
}
```

Then restart the WPF app.

Notes:

- `ApiAllowRemote=false` keeps the API on `127.0.0.1` only.
- If `ApiAllowRemote=true` but `ApiKey` is empty, the app falls back to localhost-only mode.
- Remote requests must include header: `X-Api-Key: <your token>`.
- Loopback requests from the same PC still work without the header.

Python example from another PC:

```python
from vilssharpx import VilsClient

client = VilsClient(base_url="http://192.168.1.50:8420", api_key="change-me-lab-token")
print(client.ping())
```

PowerShell example from another PC:

```powershell
$base = "http://192.168.1.50:8420/api/v1/command"
Invoke-RestMethod -Uri $base -Method Post -ContentType "application/json" -Headers @{ "X-Api-Key" = "change-me-lab-token" } -Body '{"requestId":"1","command":"Ping","payload":{}}'
```

See [`examples/basic_test.py`](examples/basic_test.py) for a full smoke test.
