# cmd/attester Go Design

## 1. Purpose
This document defines the Go-side APIs and user-visible behavior for Web/CLI.
The Go layer acts as the REE-side frontend: it accepts Web/CLI requests and calls C APIs in `attester/App`.
It also converts C API results into HTTP responses and user-facing messages.

## 2. Scope
| File | Responsibility |
| --- | --- |
| `attester_c_bridge.go` | Handles C API results (`teep_session_result_t`) as Go `TeepSessionResult` and provides REE-side Go APIs |
| `web.go` | Runs install via `POST /teep` and decides HTTP responses (`200`/`500`) |
| `web_template.html` | Displays Web UI messages based on `/teep` response |
| `main.go` | Provides `web` / `cli` entry points, subcommand routing, and startup options |

## 3. TEEP Session Result
Install processing returns one final status value, represented by `teep_session_result_t`.
On the Go side, this C enum is handled as `TeepSessionResult`.

| `teep_session_result_t` | Go-side handling |
| --- | --- |
| `TEEP_SESSION_RESULT_OK` | Success |
| `TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED` | Success (activation flow completed) |
| `TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE` | Returned as `error` |
| `TEEP_SESSION_RESULT_FATAL` | Returned as `error` |
| `TEEP_SESSION_RESULT_HTTP_ERROR` | Returned as `error` |

### Error Details Handling
`teep_session_result_t` (Go: `TeepSessionResult`) is used only for internal success/failure decisions.
This value itself is not shown to end users.
For failures, the Web UI shows a fixed message: `install failed`.
Detailed failure causes are checked in Attester logs (stdout/stderr): same terminal in CLI mode, server console in Web mode.

## 4. Go Bridge API
These Go bridge APIs are defined in `cmd/attester/attester_c_bridge.go`.
They call C APIs declared in `App/inc/attester_api.h` and implemented in `App/src/attester_api.cpp`.

| Function | Return Value | Purpose |
| --- | --- | --- |
| `InitializeEnclave()` | `error` | Initializes Enclave and prepares keys |
| `RunInstallSession()` | `(TeepSessionResult, error)` | Runs install session with TAM URL and wappName, then returns session result |
| `InvokeWasm()` | `([]byte, error)` | Runs specified `wapp`/`func` and returns inference output bytes |
| `Close()` | `void` | Closes the Enclave session and releases resources |

## 5. Web API List
| Method | Path | Purpose | Main Response |
| --- | --- | --- | --- |
| `GET` | `/` | Returns Web UI (HTML) | `200` (`text/html`) |
| `POST` | `/detect` | Runs image inference | `200` (processed image) / `400` / `500` |
| `POST` | `/teep` | Runs install session | `200` / `500` |

### 5.1 `POST /teep` Behavior
| Item | Description |
| --- | --- |
| Input | None (POST with empty body) |
| Success (`TEEP_SESSION_RESULT_OK`) | `200` / `TEEP install finished.` |
| Success (`TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED`) | `200` / `The device has been activated. You can install the app.` |
| Failure | `500` / `install failed` |

### 5.2 `POST /detect` Behavior
| Item | Description |
| --- | --- |
| Input | `image` in `multipart/form-data` |
| Success | `200` / `image/jpeg` (processed image) |
| Failure (invalid input) | `400` (e.g., `missing image`, `empty image`) |
| Failure (execution failure) | `500` (`invoke detector failed`) |

## 6. Web UI Display Rules
| HTTP Response | UI Message |
| --- | --- |
| `200` | Show response body text |
| non-`200` | `TEEP install failed. Check server logs.` |

## 7. CLI Display Rules
| Condition | CLI Output |
| --- | --- |
| `TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED` | `The device has been activated. You can install the app.` |
| `TEEP_SESSION_RESULT_OK` | `TEEP install finished.` |
| Error (other than the two success results above) | Show the `error` string returned by `RunInstallSession()` |
