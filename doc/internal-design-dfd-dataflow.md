# Internal Design DFD Dataflow Details

## 1. Purpose
This document helps engineers inspect data flow within their own responsibility area (REE, TEE, or REE-TEE boundary).
It defines file/module-level data exchanges for impact analysis and design review.

## 2. Scope
- In scope: REE <-> TEE data flow for WASM invocation and TEEP session handling.
- In scope: TAM communication path and SUIT-to-record storage path.
- Out of scope: line-by-line implementation details.

## 3. Split DFD Views
### 3.1 REE DFD
```mermaid
flowchart LR
  User[User]
  Browser[Web Browser]
  TAM[TAM /tam]

  subgraph Device[TEE Device]
  direction LR
    subgraph REE[REE]
    direction LR
      subgraph GoLayer[cmd/taws]
      direction TB
        Main([main.go])
        Web([web.go])
        Bridge([attester_c_bridge.go])
      end

      subgraph CLayer[App/src]
      direction TB
        CAPI([attester_api.cpp])
        Session([sgx_teep_session.cpp])
        HTTP([teep_http_client.cpp])
      end
    end

    subgraph TEE[TEE]
    direction TB
      TEEEntry["Enclave"]
    end
  end


  User -->|input_image| Browser
  Browser -->|HTTP: input_image| Web
  Web -->|HTTP: html, output_image| Browser
  Main -->|wappName, funcName| Web
  Web -->|wappName, funcName, input_image| Bridge
  Bridge -->|output_image| Web
  Bridge -->|wappName, funcName, input_image| CAPI
  CAPI -->|output_image| Bridge
  CAPI -->|wappName| Session
  CAPI -->|ECALL: wappName, funcName, input_image| TEEEntry
  TEEEntry -->|output_image| CAPI

  Session -->|QueryResponse / SuccessMessage| HTTP
  HTTP -->|QueryRequest / UpdateMessage| Session
  HTTP -->|HTTP: QueryResponse / SuccessMessage| TAM
  TAM -->|HTTP: QueryRequest / UpdateMessage| HTTP
  Session -->|ECALL:<br/> QueryRequest / UpdateMessage| TEEEntry
  TEEEntry -->|QueryResponse / SuccessMessage| Session
```

### 3.2 TEE DFD
```mermaid
flowchart LR

  subgraph Device[TEE Device]
  direction LR
    subgraph REE[REE]
    direction TB
      REEEntry[REE]
    end

    subgraph TEE[TEE]
    direction TB
      EPM([Enclave_process_message.cpp])
      SP([suit_processor_wrapper.cpp])
      TCM([tc_manager.cpp])
      Record[(manifest_record table)]
      EWASM([Enclave_wasm.cpp])
      Evidence([Enclave_generate_evidence.cpp])
      AgentKey([Enclave_generate_keypair.cpp])
      AttesterKey[(attester_es256_key.h)]
      SuitKey[(suit_manifest_prime256v1_cose_key_public.h)]

    end
  end

  REEEntry -->|"ECALL:<br/> QueryRequest / UpdateMessage"| EPM
  EPM -->|"QueryResponse / SuccessMessage"| REEEntry
  REEEntry -->|ECALL: wapp_name, funcName, input_image| EWASM
  EWASM -->|output_image| REEEntry

  AgentKey -->|AgentPublicKey, AgentPrivateKey| EPM
  SuitKey -->|SuitPrivateKey| EPM

  AttesterKey -->|AttesterPrivateKey| Evidence
  Evidence -->|Evidence| EPM
  EPM -->|UpdateMessage.manifest| SP
  SP -->|wapp_name,wapp_bin| TCM
  TCM <--> |wapp_name,wapp_bin| Record
  EWASM -->|lookup: wapp_name| TCM
  TCM -->|wapp_bin| EWASM
```

### 3.3 REE-TEE Boundary DFD

```mermaid
flowchart LR
  CAPI[App/src/attester_api.cpp]
  Session[App/src/sgx_teep_session.cpp]
  EPM[Enclave_process_message.cpp]
  EWASM[Enclave_wasm.cpp]

  CAPI -->|ECALL appName,func,input_image| EWASM
  EWASM -->|output_image| CAPI

  Session -->|ECALL teep message| EPM
  EPM -->| teep message| Session
```
