# SUIT Processor Design

## 1. Purpose
This document describes the behavior of this module, which wraps SUIT callbacks.

## 2. Scope
- Target implementation: `Enclave/src/suit_manifest_process.cpp`
- Public header: `Enclave/inc/suit_manifest_process.h`

## 3. Module Overview
This module is a wrapper layer around `libcsuit` callbacks.
It extends callback behavior to match TEE-side implementation requirements.
In this document, the “SUIT processor” means the `libcsuit` execution engine that parses a SUIT envelope, verifies it, evaluates conditions, and applies the manifest command sequence.
This module does not replace that engine; it connects the processor to Attester-specific storage, verification context, and report handling.

Main roles:
- Store callback: bridges SUIT store events to `tc_manager`
- Condition callback: checks whether received expected values (size and hash) match binary information stored in TEE
- Report callback: stores generated SUIT reports in an internal buffer

## 4. Interface
### 4.1 Public Function

```c
const uint8_t *suit_get_suit_report(size_t *len)
```
- Purpose: Get the most recently generated and stored SUIT report.
- Argument:
  - `len`: Output pointer for report length. If `NULL`, length is not returned.
- Return value: Pointer to the start of the report buffer (`NULL` if no report is stored)

`__wrap_*` callback functions are internal implementations for `libcsuit` integration and are described in Section 5.

### 4.2 Main Input Structures

This section lists only the main input structures needed to understand callback behavior.

#### `suit_store_args_t` (store callback input)
| Field | Type | Description |
| --- | --- | --- |
| `operation` | `suit_store_operation_t` | Processing type. Store processing runs only when this is `SUIT_STORE`. |
| `manifest_digest` | `UsefulBufC` | Key used to identify the target record. |
| `is_manifest_itself` | `bool` | `true`: Manifest-side data, `false`: Payload-side data. |
| `manifest_sequence_number` | `uint64_t` | Manifest version. |
| `dst` | `UsefulBufC` | SUIT component_id (CBOR). The name is restored after decoding. |
| `src_buf` | `UsefulBufC` | Source data to be stored. |

#### `suit_condition_args_t` (condition callback input)
| Field | Type | Description |
| --- | --- | --- |
| `condition` | `suit_condition_t` | Condition to evaluate. Main target is `SUIT_CONDITION_IMAGE_MATCH`. |
| `expected.u64` | `uint64_t` | Expected image size. |
| `expected.str` | `UsefulBufC` | Expected digest (CBOR-encoded SUIT_Digest that includes algorithm ID and hash value). |
| `manifest_digest` | `UsefulBufC` | Manifest identifier. |

#### `suit_callback_ret_t` (condition callback output)
| Field | Type | Description |
| --- | --- | --- |
| `reason` | `suit_report_reason_t` | Failure reason used in SUIT report. |
| `consumed_parameter_keys[]` | `int32_t[]` | SUIT parameter keys consumed during evaluation. |

#### `suit_report_args_t` (report callback input)
| Field | Type | Description |
| --- | --- | --- |
| `suit_report` | `UsefulBufC` | Generated (protected) SUIT report. |

## 5. Callback Behavior
### 5.1 `__wrap_suit_store_callback`
- Purpose: Pass SUIT store events to `tc_manager_store_record_from_store_args`.
- Argument:
  - `store_args`: Store callback input (`suit_store_args_t`)
- Behavior:
  - Runs store processing only when `operation == SUIT_STORE`
  - Returns `SUIT_ERR_FATAL` if storing fails
- Note: See [tc-manager.md](./tc-manager.md) for `tc_manager` behavior.

### 5.2 `__wrap_suit_condition_callback`
- Purpose: Evaluate `SUIT_CONDITION_IMAGE_MATCH` using data stored on the TEE side.
- Arguments:
  - `condition_args`: Condition callback input (`suit_condition_args_t`)
  - `condition_ret`: Output for evaluation result (`suit_callback_ret_t`)
- Image size and digest check order:
  1. `wapp_bin` exists
  2. If `expected.u64 != 0`, size matches
  3. `expected.str` can be decoded as digest
  4. Digest is SHA-256 with 32-byte length
  5. Digest bytes match `record->wapp_hash`
- On mismatch:
  - Set `condition_ret.reason` and return `SUIT_ERR_CONDITION_MISMATCH` (or related error)
  - Set `consumed_parameter_keys` to `SUIT_PARAMETER_IMAGE_SIZE` and `SUIT_PARAMETER_IMAGE_DIGEST`
  - These keys indicate which parameters were used for evaluation and are propagated to SUIT report processing

### 5.3 `__wrap_suit_report_callback`
- Purpose: Keep generated SUIT reports for later access.
- Argument:
  - `report_args`: Report callback input (`suit_report_args_t`)
- Behavior:
  - If `report_args.suit_report` is not empty, copy it into an internal buffer
  - Free existing buffer before overwrite
- Internal state:
  - `g_suit_report` (`UsefulBuf`): storage for the latest copied SUIT report (`ptr` and `len`)
- Public getter API:
  - `suit_get_suit_report(size_t *len)` returns the stored pointer and length
  - Returns `NULL` / `0` when no report is stored
- Lifecycle:
  - Updated every time report callback is called
  - Old buffer is freed before update
- Failure handling:
  - Returns `SUIT_ERR_WHILE_REPORTING` when lower-level callback fails
  - Returns `SUIT_ERR_NO_MEMORY` when memory allocation fails

## 6. Test
### 6.1 Unit Test
Target file: `Enclave/tests/suit_condition_callback_test.cpp`

| Test item | Covered case |
| --- | --- |
| Payload not stored | Manifest record exists but `wapp_bin` is not stored. Returns `SUIT_ERR_CONDITION_MISMATCH` and `reason=SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED`. |
| Size mismatch | `expected.u64` and stored size do not match. Returns `SUIT_ERR_CONDITION_MISMATCH` and `reason=SUIT_REPORT_REASON_CONDITION_FAILED`. |
| Digest mismatch | Digest in `expected.str` and stored `wapp_hash` do not match. Returns `SUIT_ERR_CONDITION_MISMATCH` and `reason=SUIT_REPORT_REASON_CONDITION_FAILED`. |
| Unsupported digest algorithm | Algorithm in `expected.str` is not SHA-256 (for example SHA-384). Returns `SUIT_ERR_NOT_IMPLEMENTED` and `reason=SUIT_REPORT_REASON_COMPONENT_UNSUPPORTED`. |
| Success case | Size and digest both match. Returns `SUIT_SUCCESS`. |
