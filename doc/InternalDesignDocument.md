# Internal Design Document

## 1. Purpose and Scope
This document provides an internal overview of the TEE Device design.
Detailed specifications for each module are documented in separate internal design documents.
This document focuses on high-level flow and module responsibilities.

## 2. Architecture Overview
### 2.1 Components and Roles inside the TEE Device
- `cmd/attester` (Go):
  - CLI/Web entry points
  - Calls App-layer APIs through the C bridge
- `App` (REE-side C/C++):
  - Calls Enclave ECALLs
  - Controls TEEP message send/receive flow
- `Enclave` (TEE-side C/C++):
  - Handles Query/Update processing as a TEEP Agent
  - Runs SUIT processing through SUIT-Manifest-Processor
  - Manages Trusted Components (TC) through TC-Manager
  - Executes WASM through WAMR

TODO: Update this figure to a simpler DFD.
```mermaid
flowchart LR

%% ================= External =================
User["👤User"]
Browser[Web Browser]

subgraph TAM["TAM (External)"]
  TAMNode[TAM]
end

User -->|"Web-based operation<br/>(image)"| Browser
Browser -->|"Web-based operation result<br/>(image)"| User

User -->|"Command-line operation<br/>(appName, wasmFunc, image)"| go_main
go_main -->|"Command-line operation result<br/>(image)"| User


%% ================= TEE Device =================
subgraph Device["TEE Device"]
direction LR

  %% ================= REE =================
  subgraph REE["REE (Host)"]
  direction LR

    %% ---- cmd/attester  ----
    subgraph cgo["cmd/attester"]
    direction TB
      go_main(("main.go (cli/web)"))
      go_web(("web.go"))
      go_bridge(("attester_c_bridge.go"))

      go_main -->|"If CLI execution<br/>tamURL, appName, wasmFunc, image"| go_bridge
      go_main -->|"If Web execution<br/>appName, wasmFunc, tamURL"| go_web

      go_web -->|"tamURL, appName, wasmFunc, image"| go_bridge
      go_bridge -->|"image"| go_web

      Browser -->|"image"| go_web
      go_web -->|"web_template.html, image"| Browser

      go_bridge -->|"image"| go_main
    end

    %% ---- App/src ----
    subgraph CApp["App/src (C/C++)"]
    direction TB
      c_api(("attester_api.cpp"))
      teep_session(("sgx_teep_session.cpp"))
      teep_http(("teep_http_client.cpp"))
    end

  end

  %% ================= TEE =================
  subgraph TEE["TEE"]
  direction LR
    subgraph EnclaveSide["Enclave/src"]
    direction TB
      E_wasm(("Enclave_wasm.cpp"))
      E_keygen(("Enclave_generate_keypair.cpp"))
      E_keygen_record[(agent_key_global_variable)]
      E_teep(("Enclave_process_message.cpp"))
      E_tc-manager(("tc_manager.cpp"))
      E_manifest_record[(manifest_record)]
      E_suit(("suit_manifest_processor.cpp"))
      E_evidence(("Enclave_generate_evidence.cpp"))
    end
  end

end


%% ================= WASM invocation path =================
go_bridge -->|"appName, wasmFunc, image"| c_api
c_api -->|"appName, wasmFunc, image"| E_wasm
E_wasm --> |"appName"|E_tc-manager
E_tc-manager -->|"wapp_bin"|E_wasm
E_wasm -->|"image (result)"| c_api
c_api -->|"image (result)"| go_bridge

c_api -->|"keygen request"| E_keygen
E_keygen -->|"agent_public_key<br/>agent_private_key"| E_keygen_record
E_keygen_record -->|"agent_public_key"| E_teep

%% ================= TEEP session path =================
c_api -->|"appName"| teep_session

teep_session -->|"QueryRequest / UpdateMessage"| E_teep
E_teep -->|"nonce(from QueryRequest.challenge), agent_public_key"| E_evidence
E_evidence -->|"Evidence"| E_teep
E_teep -->|"QueryResponse / SuccessMessage"| teep_session

teep_session -->|"QueryResponse / SuccessMessage"| teep_http
teep_http -->|"QueryRequest / UpdateMessage"| TAMNode
TAMNode -->|"QueryResponse / SuccessMessage"| teep_http
teep_http -->|"QueryRequest / UpdateMessage"| teep_session

%% ================= SUIT manifest processing path =================
E_teep -->|"Manifest(from UPDATE.manifest_list)"| E_suit
E_suit -->|"Manifest"|E_tc-manager
E_tc-manager <-->|"manifest_digest<br/>manifest_name<br/>manifest_sequence_number<br/>manifest_bin
    wapp_name</br>wapp_hash<br/>wapp_bin"| E_manifest_record


```

## 3. Processing Flow Overview
### 3.1 TEEP Flow
- A TEEP message received on the REE side is passed to the TEEP Agent in the Enclave.
- The TEEP Agent verifies the received COSE-signed TEEP message.
  After successful verification, it checks the message `type` and runs `QueryRequest` or `UpdateMessage` processing.
  - `type == QUERY_REQUEST`: checks whether the request is supported by this implementation, and generates a QueryResponse (including Evidence and TC information when required). If the request is not supported, it generates `TEEP_ERROR`.
  - `type == UPDATE`: verifies and processes the received manifest with SUIT. On success, it stores or updates app information in records managed by TC-Manager. On failure, it returns `TEEP_ERROR`.
- On success, it returns a SUCCESS message, and the target app becomes executable in the Enclave.
- Details:
  - Query/Update processing: [enclave-process-message.md](./enclave-process-message.md)
  - SUIT processing: [suit-processor.md](./suit-processor.md)
  - TC management: [tc-manager.md](./tc-manager.md)

### 3.2 WASM Execution Flow
- Preconditions:
  - The TC-Manager records already contain information for the target app (identifier, binary, etc.).
- REE-side processing:
  - A REE API (for example, `ecall_invoke_wasm`) is called to start the Enclave ECALL for WASM execution.
- Enclave-side processing:
  - The app name from the REE side is used to find the target app in TC-Manager records.
  - The target WASM binary is loaded and executed with WAMR.
- Return:
  - Execution results (success/failure and output data) are returned to the REE side through ECALL.
- Details: [invoke_wasm.md](./invoke_wasm.md)

## 4. Module Design Documents
| Module | Role | Design Document |
| --- | --- | --- |
| `App` | Aggregates TEEP session results and returns public APIs | [attester-app-design.md](./attester-app-design.md) |
| `cmd/attester` | Go bridge APIs and Web/CLI display control | [cmd-attester-go-design.md](./cmd-attester-go-design.md) |
| `Enclave_process_message` | Processes TEEP messages | [enclave-process-message.md](./enclave-process-message.md) |
| `tc_manager` | Manages TC records | [tc-manager.md](./tc-manager.md) |
| `suit_manifest_process` | Processes SUIT manifests | [suit-processor.md](./suit-processor.md) |
| `invoke_wasm` | Controls WASM execution | [invoke_wasm.md](./invoke_wasm.md) |
