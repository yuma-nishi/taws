# TAWS: A TEEP Agent for Wasm on Intel SGX

TAWS (pronounced "tohz") contains an implementation of a TEEP Agent.
The original design goal of the TEEP Agent is to support installation and update of any WebAssembly (Wasm) applications inside a TEE.
However, in the current implementation, the TEE Device is specialized for executing a YOLOv8 WebAssembly module for image processing.
Therefore, any Wasm applications are not supported at this stage.
The TAM and Verifier components are maintained in separate repositories.
This repository focuses on building and running the TEEP Agent in Intel SGX simulation mode.
By combining this SGX-based TEEP Agent with the corresponding TAM, the full TEEP provisioning flow can be simulated.


![architectureFig](doc/images/architecture-fig.png)

## Directory Structure

````
📁 taws
├── 📁 App (application sources)
├── 📁 Enclave (enclave sources)
├── 📁 yolov8-frontend (Go CLI + Web-UI implementation and entrypoint)
├── 📁 doc (documentation assets)
├── 📁 common (Shared definitions and interfaces used by both App and Enclave)
├── 📁 scripts (build helper scripts)
├── 📁 third_party (TEEP dependencies tracked as git submodules)
│   ├── 📁 libcsuit
│   ├── 📁 libteep
│   ├── 📁 QCBOR
│   ├── 📁 t_cose
│   ├── 📁 intel-sgx-ssl
│   └── 📁 wasm-micro-runtime
├── 📄 Makefile
├── 📄 Makefile.test
└── 📄 README.md
````

The TEE Device uses the following libraries.
* [libcsuit](https://github.com/kentakayama/libcsuit)
* [libteep](https://github.com/kentakayama/libteep)
* [QCBOR](https://github.com/laurencelundblade/QCBOR)
* [t_cose](https://github.com/laurencelundblade/t_cose)
* [intel-sgx-ssl](https://github.com/intel/intel-sgx-ssl)
* [wasm-micro-runtime](https://github.com/bytecodealliance/wasm-micro-runtime)



## Getting started

### Prerequisites

You need an environment where Intel SGX simulation mode is available.
See [linux-sgx](https://github.com/intel/confidential-computing.sgx) for details.

### Build and Run the TEEP Agent

- The host build has been tested on **Ubuntu 22.04 LTS**. Other Linux distributions may work but have not been verified.
- For a host-only workflow, install dependencies locally, build the project, launch the TAM server, and then run the teep agent from the host environment.

```bash
# install third_party dependencies
cd scripts/ && ./build_third_party.sh

# build
cd .. && make SGX_MODE=SIM

# run the attester Web UI on the host
./build/go/taws web 
```

For Web Server usage (with diagram), CLI usage details, and full options, see [User Manual](./doc/USER_MANUAL.md) (especially [Web Server](./doc/USER_MANUAL.md#web-server)).


## Design Documents

The following table summarizes design documents by audience and intent.

| Document | Audience | Purpose |
| --- | --- | --- |
| [ExternalDesignDocument.md](./doc/ExternalDesignDocument.md) | TAM developers / external integrators | External behavior and interface overview of the TEE Device. |
| [external-design-web-ui-api.md](./doc/external-design-web-ui-api.md) | Web/CLI client implementers | Web UI API contract (`/teep`, `/detect`) and UI-facing error behavior. |
| [external-design-teep-tam-exchange.md](./doc/external-design-teep-tam-exchange.md) | TAM developers | TEEP-over-HTTP exchange contract between TEE Device and TAM. |
| [InternalDesignDocument.md](./doc/InternalDesignDocument.md) | TEE maintainers | Internal architecture overview, high-level flow, and module responsibilities. |
| [enclave-process-message.md](./doc/enclave-process-message.md) | TEE maintainers | High-level flow and state behavior of `ecall_process_message`. |
| [suit-processor.md](./doc/suit-processor.md) | TEE maintainers | SUIT callback-wrapper flow, entry points, and failure behavior summary. |
| [tc-manager.md](./doc/tc-manager.md) | TEE maintainers | TC record lifecycle, update policy, and API role summary. |
| [invoke_wasm.md](./doc/invoke_wasm.md) | TEE maintainers | `ecall_invoke_wasm` flow, failure behavior summary, and test coverage summary. |


# Acknowledgement

This work was supported by JST K Program Grant Number JPMJKP24U4, Japan.
