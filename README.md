# TAWS: A TEEP Agent for Wasm on Intel SGX

TAWS (pronounced "tohz") contains an implementation of a TEEP Agent.
The original design goal of the TEEP Agent is to support installation and update of any WebAssembly (Wasm) applications inside a TEE.
However, in the current implementation, the TEE Device is specialized for executing a YOLOv8 WebAssembly module for image processing.
Therefore, any Wasm applications are not supported at this stage.
The TAM and Verifier components are maintained in separate repositories.
This repository focuses on building and running the TEEP Agent in Intel SGX hardware mode.
By combining this SGX-based TEEP Agent with the corresponding TAM and Verifier, the full TEEP provisioning flow can be executed with SGX DCAP evidence.


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
│   ├── 📁 wasm-micro-runtime
│   └── 📁 intel-dcap-pccs
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
* [intel-dcap-pccs](https://github.com/intel/confidential-computing.tee.dcap.pccs)



## Getting started

### Clone the Repository

This repository uses git submodules for `third_party` dependencies.

```bash
git clone --recurse-submodules https://github.com/yuma-nishi/taws.git
cd taws
```

### Prepare SGX DCAP/PCCS Environment

- You need an environment where Intel SGX hardware mode and DCAP quote generation are available.
  See [confidential-computing.sgx](https://github.com/intel/confidential-computing.sgx) for details.
- Required version: Go >= 1.22.
- The host build has been tested on **Ubuntu 24.04 LTS**. Other Linux distributions may work but have not been verified.
- The TAWS host must have the Intel SGX DCAP Quote Generation Library and the Intel SGX default Quote Provider Library installed.
  The SGX SDK alone is not enough for DCAP quote generation because TAWS links against `libsgx_dcap_ql` and uses `sgx_qe_get_quote*`.
- The SGX DCAP Quote Provider Library must be configured to reach a Provisioning Certificate Caching Service (PCCS).

When using Intel's prebuilt DCAP packages, the default Quote Provider Library package normally provides the QCNL dependency, so building `QuoteGeneration/qcnl` manually is not required.
If you build DCAP components from source, follow Intel's [QuoteGeneration](https://github.com/intel/confidential-computing.tee.dcap/tree/main/QuoteGeneration) instructions and build both the default QPL and QCNL components.

TAWS includes Intel(R) SGX/TDX PCCS as a submodule under `third_party/intel-dcap-pccs`.
Follow the PCCS setup instructions in [`third_party/intel-dcap-pccs/service/README.md`](./third_party/intel-dcap-pccs/service/README.md).
For a manual PCCS setup, run the interactive installer from `third_party/intel-dcap-pccs/service`:

```bash
cd third_party/intel-dcap-pccs/service
./install.sh
```

PCCS settings to confirm:

- `ApiKey`: Intel PCS API key used by PCCS to fetch collateral. Generate it from the [Intel Trusted Services Portal](https://api.portal.trustedservices.intel.com/provisioning-certification), then set it correctly during `install.sh` configuration.
- `proxy`: proxy URL, only if the PCCS host needs one to reach Intel PCS.
- `CachingFillMode`: cache fill mode. `LAZY` is suitable for development with internet access.

If the TAWS host uses a local or custom PCCS, confirm the installed QPL/QCNL settings in `/etc/sgx_default_qcnl.conf`:

- Set the PCCS URL to the PCCS service, for example `https://localhost:8081/sgx/certification/v4/`, if the default does not match your PCCS.
- Set `use_secure_cert` according to the PCCS TLS certificate. For local development with a self-signed PCCS certificate, `false` may be required.
- Restart any long-running TAWS process after changing QPL or PCCS settings.

Do not commit Intel PCS API keys, PCCS user/admin tokens, private TLS keys, generated local config files, or PCCS cache databases.

### Build and Run the TEEP Agent

For a host-only workflow, install dependencies locally, build the project, launch the TAM server, and then run the TEEP Agent from the host environment.

```bash
# install third_party dependencies
cd scripts/ && ./build_third_party.sh

# build for SGX hardware mode
cd .. && make SGX_MODE=HW SGX_DEBUG=1

# run the taws web server on the host
./build/go/taws web 
```
For Web Server usage (with diagram), CLI usage details, and full options, see [User Manual](./doc/USER_MANUAL.md) (especially [Web Server](./doc/USER_MANUAL.md#web-server)).

If you need a simulation-only development build, use `make SGX_MODE=SIM` instead. SGX DCAP evidence and PCCS are hardware-mode requirements.

### Build and Run with Docker for Simulation

#### Prepare SGX Base Image

This optional simulation-only workflow prepares the SGX SDK base image from [confidential-computing.sgx](https://github.com/intel/confidential-computing.sgx), which is used by the TAWS Docker build.

```bash
cd scripts/
./prepare_sgx_base_image.sh
```

#### Build and Run TAWS in Docker

This step builds the `taws` image and runs the TAWS web server in SGX simulation mode.

```bash
docker build -t taws .

# run taws web ui
docker run --rm -p 8181:8181 \
  -e TAWS_WEB_ADDR=0.0.0.0:8181 \
  -e TAWS_TAM_URL=http://127.0.0.1:8080/tam \
  taws
```


## Design Documents

Design docs are organized by audience and hierarchy.

### External Design (for TAM developers , Web/CLI client implementers)

- [ExternalDesignDocument.md](./doc/ExternalDesignDocument.md)
  - Audience: TAM developers , Web/CLI client implementers
  - Purpose: External behavior and interface overview of the TEE Device.
  - Includes:
    - [external-design-web-ui-api.md](./doc/external-design-web-ui-api.md)
      - Audience: Web/CLI client implementers
      - Purpose: Web UI API contract (`/teep`, `/detect`) and UI-facing error behavior.
    - [external-design-teep-tam-exchange.md](./doc/external-design-teep-tam-exchange.md)
      - Audience: TAM developers
      - Purpose: TEEP-over-HTTP exchange contract between TEE Device and TAM.

### Internal Design (for TEE maintainers)

- [InternalDesignDocument.md](./doc/InternalDesignDocument.md)
  - Audience: TEE maintainers
  - Purpose: Internal architecture overview, high-level flow.
  - Related details:
    - [enclave-process-message.md](./doc/enclave-process-message.md)
      - Audience: TEE maintainers / REE-side developers (ECALL callers)
      - Purpose: High-level flow and state behavior of `ecall_process_message`.
    - [suit-processor.md](./doc/suit-processor.md)
      - Audience: TEE maintainers
      - Purpose: SUIT callback-wrapper flow, entry points, and failure behavior summary.
    - [tc-manager.md](./doc/tc-manager.md)
      - Audience: TEE maintainers
      - Purpose: TC record lifecycle and record update policy.
    - [invoke_wasm.md](./doc/invoke_wasm.md)
      - Audience: TEE maintainers / REE-side developers (ECALL callers)
      - Purpose: `ecall_invoke_wasm` flow and failure behavior summary.


# Acknowledgement

This work was supported by JST K Program Grant Number JPMJKP24U4, Japan.
