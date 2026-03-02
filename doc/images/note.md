
### 2.2 ファイル間API呼び出し図
この図はattester 内でどのファイルがどの API（関数）を呼び出しているかを追跡するための図である。

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

    %% ---- cmd/attester (元の構成) ----
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

    %% ---- App/src (あなた指定版) ----
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


```cbor
tc-list: [
  {
    system-component-id: [h'6170702E7761736D'], ; "app.wasm"
    suit-parameter-image-digest: [
      -16,
      h'012345...'
    ]
  }
]
```


This diagram shows what data is exchanged around `attester/App` between the Go layer, Enclave, and TAM.
It focuses on key data paths (app_name, TEEP messages, inference input/output, and session results), not detailed control branches.

```mermaid
flowchart LR

  %% ========== External ==========
  subgraph TAM["TAM (External)"]
    TAMNode[TAM]
  end

  %% ========== TEE Device ==========
  subgraph Device["TEE Device"]
    %% ---- REE / Host ----
    subgraph REE["REE (Host)"]
      subgraph GoCLI["cmd/attester (Go)"]
        go_bridge["attester_c_bridge.go"]
      end

      subgraph CApp["App/src (C/C++)"]
        c_api(("attester_api.cpp"))
        teep_session(("sgx_teep_session.cpp"))
        teep_http(("teep_http_client.cpp"))
      end
    end

    %% ---- TEE / Enclave ----
    subgraph TEE["TEE"]
      subgraph EnclaveSide["Enclave/src"]
        E_wasm["Enclave_wasm.cpp"]
        E_keygen["Enclave_generate_keypair.cpp"]
        E_teep["Enclave_process_message.cpp"]
      end
    end
  end

  %% ========== WASM invocation path ==========
  go_bridge -->|"appName, wasmFunc, image"| c_api
  c_api -->|"appName, wasmFunc, image"| E_wasm
  E_wasm -->|"image (result)"| c_api
  c_api -->|"image (result)"| go_bridge

  c_api -->|"keygen request"| E_keygen

  %% ========== TEEP session path ==========
  c_api -->|"appName"| teep_session

  teep_session -->|"QueryRequest / UpdateMessage"| E_teep
  E_teep -->|"QueryResponse / SuccessMessage"| teep_session

  teep_session -->|"QueryResponse / SuccessMessage"| teep_http
  teep_http -->|"QueryRequest / UpdateMessage"| TAMNode
  TAMNode -->|"QueryResponse / SuccessMessage"| teep_http
  teep_http -->|"QueryRequest / UpdateMessage"| teep_session
```