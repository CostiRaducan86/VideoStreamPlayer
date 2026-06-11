param(
    [Parameter(Mandatory = $false)]
    [string]$ServerIp = "10.168.50.102",

    [Parameter(Mandatory = $false)]
    [int]$Port = 8420,

    [Parameter(Mandatory = $true)]
    [string]$ApiKey,

    # POST command name (e.g. StartSimulation, StopSimulation, Ping, PauseSimulation, ResumeSimulation)
    # If omitted, does GET /api/v1/health/details
    [Parameter(Mandatory = $false)]
    [string]$Command = "",

    # Payload as hashtable, e.g. @{ fps = 50 }
    [Parameter(Mandatory = $false)]
    [hashtable]$Payload = @{}
)

$ErrorActionPreference = "Stop"

function Write-Info([string]$msg) { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "[OK]   $msg" -ForegroundColor Green }
function Write-Fail([string]$msg) { Write-Host "[FAIL] $msg" -ForegroundColor Red }

# TLS 1.2 + accept self-signed cert + bypass corporate proxy
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
[System.Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }
[System.Net.WebRequest]::DefaultWebProxy = [System.Net.GlobalProxySelection]::GetEmptyWebProxy()

function Invoke-ApiRequest {
    param([string]$Method, [string]$UriStr, [string]$Body = "")

    Write-Info "$Method $UriStr"
    $req = [System.Net.HttpWebRequest]::Create($UriStr)
    $req.Method = $Method
    $req.Proxy = [System.Net.GlobalProxySelection]::GetEmptyWebProxy()
    $req.Timeout = 15000
    $req.ReadWriteTimeout = 15000
    $req.Accept = "application/json"
    $req.Headers.Add("X-Api-Key", $ApiKey)

    if ($Method -eq "POST" -and $Body) {
        $req.ContentType = "application/json"
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Body)
        $req.ContentLength = $bytes.Length
        $stream = $req.GetRequestStream()
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Close()
    }

    try {
        $resp = [System.Net.HttpWebResponse]$req.GetResponse()
        $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
        $responseBody = $reader.ReadToEnd()
        $reader.Close(); $resp.Close()
        Write-Ok "HTTP $([int]$resp.StatusCode) $($resp.StatusDescription)"
        $responseBody | ConvertFrom-Json
    }
    catch [System.Net.WebException] {
        $ex = $_.Exception
        Write-Fail "WebException: $($ex.Message)"
        if ($ex.Response -ne $null) {
            $errResp = [System.Net.HttpWebResponse]$ex.Response
            $reader = New-Object System.IO.StreamReader($errResp.GetResponseStream())
            $errBody = $reader.ReadToEnd()
            $reader.Close(); $errResp.Close()
            Write-Fail "HTTP $([int]$errResp.StatusCode)"
            if ($errBody) { Write-Host $errBody }
        }
        throw
    }
}

$base = "https://$ServerIp`:$Port"

if ([string]::IsNullOrWhiteSpace($Command)) {
    # No command → health check
    Invoke-ApiRequest "GET" "$base/api/v1/health/details"
} else {
    $bodyObj = @{ command = $Command }
    if ($Payload.Count -gt 0) { $bodyObj.payload = $Payload }
    $json = $bodyObj | ConvertTo-Json -Compress -Depth 4
    Invoke-ApiRequest "POST" "$base/api/v1/command" $json
}


$ErrorActionPreference = "Stop"

function Write-Info([string]$msg) {
    Write-Host "[INFO] $msg" -ForegroundColor Cyan
}

function Write-Ok([string]$msg) {
    Write-Host "[OK]   $msg" -ForegroundColor Green
}

function Write-Fail([string]$msg) {
    Write-Host "[FAIL] $msg" -ForegroundColor Red
}

$uri = "https://$ServerIp`:$Port$Endpoint"
Write-Info "Target URI: $uri"

# Force TLS 1.2 (PS 5.1 / .NET Framework compatibility)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Accept self-signed certificates (automation/lab scenario)
[System.Net.ServicePointManager]::ServerCertificateValidationCallback = { $true }

# Force no proxy for this process/session
[System.Net.WebRequest]::DefaultWebProxy = [System.Net.GlobalProxySelection]::GetEmptyWebProxy()
Write-Info "Process proxy disabled via GetEmptyWebProxy()"

# Optional diagnostic: show what system proxy would be for this URI
try {
    $u = [Uri]$uri
    $systemProxy = [System.Net.WebRequest]::GetSystemWebProxy().GetProxy($u).AbsoluteUri
    Write-Info "System proxy resolution for target: $systemProxy"
} catch {
    Write-Info "System proxy resolution not available: $($_.Exception.Message)"
}

if ($UseInvokeRestMethod) {
    Write-Info "Using Invoke-RestMethod mode"
    $headers = @{ "X-Api-Key" = $ApiKey }
    $resp = Invoke-RestMethod -Uri $uri -Headers $headers
    Write-Ok "Invoke-RestMethod call succeeded"
    $resp | ConvertTo-Json -Depth 6
    return
}

# Default mode: HttpWebRequest (more reliable on PS 5.1 with HTTPS+proxy edge cases)
Write-Info "Using HttpWebRequest mode (PS5-safe)"

$request = [System.Net.HttpWebRequest]::Create($uri)
$request.Method = "GET"
$request.Proxy = [System.Net.GlobalProxySelection]::GetEmptyWebProxy()
$request.Timeout = 15000
$request.ReadWriteTimeout = 15000
$request.Accept = "application/json"
$request.Headers.Add("X-Api-Key", $ApiKey)

try {
    $response = [System.Net.HttpWebResponse]$request.GetResponse()
    $statusCode = [int]$response.StatusCode
    $statusText = $response.StatusDescription
    $reader = New-Object System.IO.StreamReader($response.GetResponseStream())
    $body = $reader.ReadToEnd()
    $reader.Close()
    $response.Close()

    Write-Ok "HTTP $statusCode $statusText"
    $body
}
catch [System.Net.WebException] {
    $ex = $_.Exception
    Write-Fail "WebException: $($ex.Message)"

    if ($ex.Response -ne $null) {
        $resp = [System.Net.HttpWebResponse]$ex.Response
        Write-Fail "HTTP $([int]$resp.StatusCode) $($resp.StatusDescription)"
        $reader = New-Object System.IO.StreamReader($resp.GetResponseStream())
        $errBody = $reader.ReadToEnd()
        $reader.Close()
        $resp.Close()

        if (-not [string]::IsNullOrWhiteSpace($errBody)) {
            Write-Host $errBody
        }
    }

    throw
}
