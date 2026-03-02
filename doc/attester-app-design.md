# App Design (REE Side)

## 1. Purpose
This document explains the main behavior of `App` on the REE side and defines how TEEP session results (`teep_session_result_t`) are used.
`App` is the REE-side C API layer. It receives requests from upper layers (such as Go), calls TEE-side processing (TEEP and WASM execution), and returns results in a form upper layers can handle.

## 2. Scope
This document covers implementations related to TEEP processing and WASM execution in `App`.

| File | Main Responsibility |
| --- | --- |
| `App/src/attester_api.cpp` | Calls `run_teep_session()` from `attester_install()` and returns results |
| `App/src/sgx_teep_session.cpp` | Aggregates HTTP/ECALL results in `run_teep_session()` and decides `teep_session_result_t` |
| `App/src/teep_http_client.cpp` | Sends/receives TEEP over HTTP via `teep_send_http_post()` |

## 3. Session Result Codes
`teep_session_result_t` represents how one TEEP session ended.
Go uses this value to decide success/failure handling.

| Value | Meaning |
| --- | --- |
| `TEEP_SESSION_RESULT_OK` | Session completed successfully |
| `TEEP_SESSION_RESULT_TEEP_ERROR_RESPONSE` | A TEEP Error response was generated during the session |
| `TEEP_SESSION_RESULT_FATAL` | A fatal failure occurred in the App/Enclave path |
| `TEEP_SESSION_RESULT_HTTP_ERROR` | HTTP communication/status failed |
| `TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED` | Session completed successfully and device activation flow was observed |

## 4. C APIs for Go Integration
The APIs below are provided to Go.
Declarations are in `App/inc/attester_api.h`, and implementations are in `App/src/attester_api.cpp`.

| API | Return Type | Description |
| --- | --- | --- |
| `attester_install(const char *tam_url, const char *app_name)` | `teep_session_result_t` | Runs a TEEP session using the specified TAM URL and app name, then returns the session result |
| `attester_init(const char *keygen_mode)` | `int` | Initializes Enclave and prepares signing keys. `0` means success; non-zero means failure |
| `attester_invoke_wasm(const char *wapp_name, const char *func_name, const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len, size_t *actual_len)` | `int` | Executes a WASM function, writes output bytes to `output`, and writes actual output size to `actual_len`. `0` means success; non-zero means failure |

## 5. Notes
- `TEEP_SESSION_RESULT_OK_DEVICE_ACTIVATED` means the device activation flow completed. It does not directly guarantee app installation completion by itself.
- Final user-facing messages (Web/CLI) are decided in upper layers.
- For detailed failure causes, check App process logs (stdout/stderr).
