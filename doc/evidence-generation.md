# Evidence Generation Design

## 1. Purpose and Scope
This document describes how the Enclave creates attestation evidence for a
`QueryResponse`.

- Target implementation: `Enclave/src/Enclave_generate_evidence.cpp`
- Caller: `process_query_request` in `Enclave/src/Enclave_process_message.cpp`
- In scope: Evidence generation, quote binding, and failure behavior.
- Out of scope: DCAP Quote verification details and PCCS/AESM deployment or
  configuration.

Evidence is generated only when
`QueryRequest.data_item_requested.attestation` is true.

## 2. Build-time Configuration and Output
| Build setting | Evidence format | `attestation_payload` |
| --- | --- | --- |
| `SGX_EVIDENCE=1` (default) | `application/sgx-quote3-teep-bundle` | CBOR array containing a Quote3 and raw report data |
| `SGX_EVIDENCE=0` | `application/eat+cwt; eat_profile="urn:ietf:rfc:rfc9711"` | Generic EAT payload wrapped in COSE Sign1 |

The resulting payload and its format are included in the `QueryResponse`.
The external payload contract is defined in
[external-design-teep-tam-exchange.md](./external-design-teep-tam-exchange.md).

## 3. SGX DCAP Evidence Flow
For `SGX_EVIDENCE=1`, the Enclave performs the following steps:

1. Construct raw report data from the TEEP Agent P-256 public-key coordinates
   and the `QueryRequest` challenge.
2. Compute SHA-384 over the raw report data and place the 48-byte digest in
   the beginning of the 64-byte SGX `report_data`; the remaining 16 bytes are
   zero.
3. Call an OCALL to obtain Quoting Enclave (QE) target information, then create an SGX report addressed
   to the QE.
4. Call OCALLs to obtain the required quote size and then the DCAP Quote3.
   The REE submits the SGX report to the QE, which verifies/processes the report and generates the quote.
5. Encode the Quote3 and raw report data as the following CBOR array:

```cbor-diag
[
  raw-dcap-quote3,
  raw-report-data
]
```

```mermaid
sequenceDiagram
    box TAWS execution device
        participant EG as TAWS TEE
        participant QGA as TAWS REE
        participant QE as Quoting Enclave
    end
    box Attestation-data services outside TAWS execution device
        participant PCCS as PCCS
        participant PCS as Intel PCS
        participant Azure as Azure DCAP service
    end

    Note over EG: The TEEP Agent private key remains in the TAWS TEE.<br/>Its public key is used for the report_data binding.
    Note over QE: The QE attestation key signs the Quote3.

    EG->>EG: raw-report-data = pubkey-x || pubkey-y || challenge
    EG->>EG: report_data = SHA-384(raw-report-data) || 16 zero bytes
    EG->>QGA: OCALL: get QE target info
    QGA->>QE: obtain target info
    QE-->>QGA: target info
    QGA-->>EG: target info
    EG->>EG: sgx_create_report(QE target info, report_data)
    EG->>QGA: OCALL: get quote size / get Quote3 (SGX report)
    alt Standard SGX host
        QGA->>PCCS: request PCK certificate chain<br/>and other DCAP collateral
        opt attestation data not cached
            PCCS->>PCS: obtain attestation data
            PCS-->>PCCS: attestation data
        end
        PCCS-->>QGA: PCK certificate chain<br/>and other DCAP collateral
    else Azure SGX VM
        QGA->>Azure: request PCK certificate chain<br/>and other DCAP collateral
        Azure-->>QGA: PCK certificate chain<br/>and other DCAP collateral
    end
    QGA->>QE: submit SGX report for Quote3 generation
    QE->>QE: verify/process SGX report
    QE->>QE: generate Quote3
    QE-->>QGA: Quote3
    QGA-->>EG: quote size / Quote3
    EG->>EG: CBOR encode [Quote3, raw-report-data]
```

## 4. Generic EAT Evidence
`SGX_EVIDENCE=0` is a development and compatibility mode.
`create_evidence_generic()` returns an [RFC 9711](https://datatracker.ietf.org/doc/rfc9711/)
EAT payload wrapped in COSE Sign1. The EAT confirmation claim contains the
TEEP Agent P-256 public key and key identifier. Its nonce is the
`QueryRequest` challenge when present; otherwise, the implementation uses its
default EAT nonce. For individual EAT claim formats and verification details,
refer to RFC 9711.
