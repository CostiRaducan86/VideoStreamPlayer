# VilsSharpX

WPF application (.NET 8) for real-time visualization and comparison of 8-bit grayscale
LED matrix frames from automotive ECU systems (Aurix TC397).

## Features

- **4-Pane Visualization**: AVTP Generator (A), LVDS Real Data (B), LSM Camera via Basler (C), Comparison (D)
- **Protocol Support**: AVTP/RVF (ethertype 0x22F0), LVDS via Ethernet (ethertype 0x88B5)
- **Comparison Modes**: LVDS-AVTP, LSM-LVDS, LSM-AVTP with color-coded deviation display
- **Automation REST API**: Programmatic control via HTTP/HTTPS (see below)
- **Recording**: AVI capture of all panes

## Build and Run

```powershell
cd VilsSharpX
dotnet build
dotnet run
```

**Requirements**: .NET 8 SDK, Windows (WPF), Npcap (for Ethernet capture), Basler Pylon SDK (optional, for camera)

## Automation REST API

VilsSharpX includes an embedded REST API for external test automation and scripting.

| Feature            | Description                                                      |
| ------------------ | ---------------------------------------------------------------- |
| **Transport**      | HTTP (loopback) or HTTPS (remote, self-signed TLS certificate)   |
| **Authentication** | X-Api-Key header + IP allowlist for remote clients               |
| **Default**        | `http://127.0.0.1:8420/api/v1/health`                            |
| **Commands**       | Ping, Start/Stop/Pause/Resume, SetComparison, GetStats, Snapshot |

### Quick Example (PowerShell)

```powershell
# Local — always works (even with HTTPS enabled)
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/health"

# Start simulation
$body = '{"command":"StartSimulation","payload":{"fps":100}}'
Invoke-RestMethod -Uri "http://127.0.0.1:8420/api/v1/command" -Method Post -Body $body -ContentType "application/json"
```

### Configuration

Open **Configuration > API Configuration** in the application to:

- Enable/disable remote access
- Enable HTTPS (TLS) with auto-generated self-signed certificate
- Generate and manage API keys
- Configure IP allowlist

### Full Documentation

See [docs/API.md](docs/API.md) for complete endpoint reference, authentication details,
error codes, and SDK examples.

## Project Structure

```text
VilsSharpX/
  Api/                    REST API (Kestrel host, routing, models, TLS)
  Aurix_Firmware/         Embedded C code for Aurix TC397
  docs/                   Documentation (API reference, tech docs)
  MainWindow.xaml.cs      Main UI and rendering loop
  *EthCapture.cs          Ethernet frame capture (Osram, Nichia, AVTP)
  DiffRenderer.cs         Pixel comparison algorithm
  RvfReassembler.cs       AVTP RVF frame reassembly
  BaslerCameraCapture.cs  Basler Pylon USB3 camera integration
```

## License

Proprietary. Internal use only.
