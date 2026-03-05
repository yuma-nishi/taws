# TC Manager Design

## 1. Purpose
`tc_manager` is an in-memory ledger module for Trusted Component (TC) records.
It stores manifest/payload data produced by SUIT processing and provides record lookup/list APIs for other modules.

## 2. Scope
- Target implementation: `Enclave/src/tc_manager.cpp`
- Public header: `Enclave/inc/tc_manager.h`

## 3. Module Responsibilities
- Store and update TC records keyed by `manifest_digest` (record fields: see `manifest_record_t` in `Enclave/inc/tc_manager.h`).
- Keep partial records until required elements are complete
- Finalize/discard records based on completeness and duplicate update rules
- Provide APIs that let other modules read stored records (for example, record lookup by app name/digest and TC list export for `QUERY_RESPONSE`).

## 4. Process Flow
### 4.1 Store and Finalize Flow
1. Receive SUIT store input and find or allocate a target record.
2. Store either manifest metadata or WASM payload data based on callback input.
3. Finalization step validates completeness and applies duplicate-update policy.
4. Incomplete or duplicate records with lower manifest_sequence_number are removed.

```mermaid
sequenceDiagram
    participant SUIT as SUIT store callback
    participant TCM as tc_manager_store_record_from_store_args
    participant TABLE as manifest_record table
    participant FIN as tc_manager_check_and_update_record

    SUIT->>TCM: store_args
    TCM->>TABLE: find/create record by manifest_digest
    alt manifest metadata input
        TCM->>TABLE: store manifest fields
    else WASM payload input
        TCM->>TABLE: store payload fields
    end

    SUIT->>FIN: finalize target record
    FIN->>TABLE: validate completeness
    FIN->>TABLE: apply duplicate wapp_name rule
    FIN-->>SUIT: success/failure
```

## 5. Design Policy
### 5.1 Partial Record Handling
Manifest data and payload data may arrive at different times.
`tc_manager` keeps partial records and finalizes only when required fields are complete.

### 5.2 Duplicate Update Rule
When multiple records have the same `wapp_name`, `manifest_sequence_number` is used:
- Existing `<=` New: keep new record, remove old record
- Existing `>` New: keep old record, remove new record

This keeps one latest record per `wapp_name` and prevents downgrade.

### 5.3 Deletion Strategy
On deletion, replace the target record with the last active record, then decrement the active record count by one.
Variable-size buffers (`manifest_digest`, `manifest_bin`, `wapp_bin`) are freed on delete.

## 6. Public API Summary
| API | Purpose |
| --- | --- |
| `tc_manager_store_record_from_store_args` | Store callback input into TC record |
| `tc_manager_check_and_update_record` | Finalize and apply duplicate policy |
| `tc_manager_find_record_by_digest` | Lookup by `manifest_digest` |
| `tc_manager_find_record_by_wappname` | Lookup by `wapp_name` |
| `tc_manager_get_tc_list` | Export TC list items for response generation |
| `tc_manager_record_count` | Get active record count |
| `tc_manager_dump_records` | Print current records to debug log |
| `tc_manager_remove_all` | Remove all records |

Note:
- For detailed argument/field definitions and exact error-code mapping, refer to `Enclave/inc/tc_manager.h` and `Enclave/src/tc_manager.cpp`.

## 7. Failure Behavior Summary
- Invalid input (for example `NULL`, unsupported operation): API returns failure.
- Record allocation/storage failure: API returns failure.
- Record finalization failure (missing required fields / invalid target digest): API returns failure.

## 8. Test Coverage Summary
### 8.1 Unit Tests
- Store/finalize behavior for manifest-only, payload-only, and complete-record cases
- Duplicate `wapp_name` update/discard rule by sequence number
- TC list export contract (count, capacity, invalid input)

Targets:
- `Enclave/tests/manifest_record_store_test.cpp`
- `Enclave/tests/tc_manager_get_tc_list_test.cpp`

### 8.2 Integration/Regression Tests
- Run real Update messages through the full path and verify that `tc_manager` ends with the expected record state.

Target:
- `Enclave/tests/tc_update_integration_test.cpp`

## 9. Related Documents
- TEEP message flow integration: [enclave-process-message.md](./enclave-process-message.md)
- SUIT callback wrapper behavior: [suit-processor.md](./suit-processor.md)
