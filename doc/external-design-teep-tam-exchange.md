# External Design Document - TEEP Message Exchange with TAM

## 1. Purpose and Scope
This document defines the external design required for TAM developers to integrate with the TEE device.
Scope is limited to HTTP interface behavior and message handling contract between TEE device and TAM.

## 2. Interface Contract
### 2.1 Endpoint
| Item | Description |
| --- | --- |
| Method | `POST` |
| Path | `<configured TAM URL>` |
| Request body | COSE-wrapped TEEP message (CBOR) |
| Response body (`200 OK`) | COSE-wrapped TEEP message (CBOR) |



## 3. Message Specification
Message details in this section follow [IETF TEEP Protocol draft section 4](https://datatracker.ietf.org/doc/html/draft-ietf-teep-protocol-21#section-4).

### 3.1 Main Outgoing Message Types (TEE device -> TAM)
- `QueryResponse`
- `SuccessMessage`
- `ErrorMessage`

### 3.2 Main Incoming Message Types (TAM -> TEE device)
- `QueryRequest`
- `UpdateMessage`

### 3.3 Message Handling Mapping
| Incoming message from TAM | TEE device behavior | Outgoing message from TEE device |
| --- | --- | --- |
| No TEEP message (initial session start) | Start session by sending `POST /tam` with empty body; wait for TAM's first TEEP message | Empty HTTP request body (typically expecting `QueryRequest` in `200 OK` response) |
| `QueryRequest` | Process requested capabilities/attestation query and continue session | `QueryResponse` |
| `UpdateMessage` | Process update payload and continue or finalize session based on result | `SuccessMessage` or `ErrorMessage` |

Supplement: The initial empty-body `POST` follows TEEP over HTTP draft behavior (see [section 5.1.1](https://www.ietf.org/archive/id/draft-ietf-teep-otrp-over-http-15.html#section-5.1.1) and [section 6.1](https://www.ietf.org/archive/id/draft-ietf-teep-otrp-over-http-15.html#section-6.1)).

## 4. Status Codes and Session Handling
- `200 OK`: Continue the TEEP session using the returned COSE-wrapped TEEP message (CBOR).
- `204 No Content`: Treat as normal session end.
- Non-2xx HTTP status (for example `500`): Treat the session as failed.
