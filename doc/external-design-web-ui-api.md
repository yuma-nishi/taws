# External Design Document - Web UI API

## 1. Purpose and Scope
This document defines the external Web UI API contract of the TEE device.
Scope is limited to HTTP request/response behavior and UI error display policy.

## 2. Assumptions and Preconditions
- Authentication and authorization are not implemented.
- HTTPS is not supported (HTTP only).
- Concurrent multiple requests are out of scope.

## 3. API Endpoints
### 3.1 `POST /teep`
| Item | Description |
| --- | --- |
| Request | Method: `POST` / Path: `/teep` / Body: empty |
| Response (`405 Method Not Allowed`) | If method is not `POST`, Body: `method not allowed` |
| Response (`200 OK`) | If the install session reaches device activation state, Body: `The device has been activated. You can install the app.` |
| Response (`200 OK`) | If the install session completes successfully, Body: `TEEP install finished.` |
| Response (`500 Internal Server Error`) | If enclave initialization fails, Body: `install failed` |
| Response (`500 Internal Server Error`) | If TEEP session fails, Body: `install failed` |

### 3.2 `POST /detect`
| Item | Description |
| --- | --- |
| Request | Method: `POST` / Path: `/detect` / Content-Type: `multipart/form-data` / Field: `image` |
| Response (`405 Method Not Allowed`) | If method is not `POST`, Body: `method not allowed` |
| Response (`200 OK`) | `image/jpeg` (processed image) |
| Response (`400 Bad Request`) | If multipart format is invalid, Body: `invalid multipart form` |
| Response (`400 Bad Request`) | If `image` is missing, Body: `missing image` |
| Response (`400 Bad Request`) | If reading `image` fails, Body: `read image failed` |
| Response (`400 Bad Request`) | If `image` is empty, Body: `empty image` |
| Response (`500 Internal Server Error`) | If inference execution fails, Body: `invoke detector failed` |

## 4. Request Limits
- `POST /detect` parses multipart form data with a maximum memory size of `16 MiB` (`r.ParseMultipartForm(16 << 20)`).

## 5. Error and UI Handling Policy
- External error response bodies are fixed strings.
- For detailed causes, clients should check TEE device process logs (stdout/stderr).
- If `POST /teep` returns `200`, the Web UI displays the response body.
- If status is not `200`, the Web UI displays `TEEP install failed. Check server logs.`
- If `POST /detect` fails, the Web UI displays `Detection failed. Check server logs.`
