# TAWS: A TEEP Agent for Wasm on Intel SGX

This repository contains the TEEP Agent implementation used in the TEEP demo.
The TAM and Verifier components are maintained in separate repositories.
This repository focuses on building and running the TEEP Agent in Intel SGX simulation mode.
By combining this SGX TEEP Agent with the corresponding TAM, you can simulate the full TEEP provisioning flow.


## Architecture

![architectureFig](doc/images/architecture-fig.png)

## Directory Structure

````
📁 attester
├── 📁 App (application sources)
├── 📁 Enclave (enclave sources)
├── 📁 cmd (CLI + Web-UI entrypoints)
│   └── 📁 attester
├── 📁 doc (documentation assets)
├── 📁 inc (headers and prebuilt libs)
├── 📁 scripts (build helper scripts)
├── 📁 tam_mock_server (local TAM mock server)
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

- Install the SGX SDK and ensure `SGX_SDK` is set (e.g. `/opt/intel/sgxsdk`).
- If you use simulation mode, make sure `SGX_MODE=SIM` is set.
- Build dependencies using `scripts/build_third_party.sh`.

### TEE Device

- The host build has been tested on **Ubuntu 22.04 LTS**. Other Linux distributions may work but have not been verified.
- For a host-only workflow, install dependencies locally, build the project, launch the TAM server, and then run the attester CLI from the host environment.

```
# install third_party dependencies
host$ cd scripts/ && ./build_third_party.sh

# build
host$ cd .. && make SGX_MODE=SIM

# run the attester Web UI on the host
host$ ./build/go/attester-go web 

# sanity check (print help and exit)
host$ ./build/go/attester-go
```



## Running & CLI Options

After building the TEE Device (see [Getting started](#getting-started)), start the CLI:

```
Usage: ./build/go/attester-go cli [--keygen yes|no]
```

### CLI Commands

```
install [--url URL] [--wapp NAME]
detector [--wapp NAME] [--func NAME] [--max-output BYTES] <input.jpg> [output.jpg]
help
exit
```

To start the Web-UI:

```
Usage: ./build/go/attester-go web [--addr ADDR] [--wapp NAME] [--func NAME] [--keygen yes|no] [--max-output BYTES] [--url URL]
```

### Options

#### Common Option

| Option | Applies to | Description |
|---------|------------|-------------|
| `--keygen yes/no` | `cli`, `web` | Configure how the TEEP Agent Key pair is prepared at startup: generate a new key pair (`yes`) or reuse an existing key (`no`). |

#### CLI Command Options

| Option | Applies to | Description |
|---------|------------|-------------|
| `install --url URL` | `install` | Override the TAM URL. Default: `http://localhost:8080/tam`. |
| `install --wapp NAME` | `install` | WASM app name for install session. Default: `yolov8.wasm`. |
| `detector --wapp NAME` | `detector` | Target WAPP name for detector command. Default: `yolov8.wasm`. |
| `detector --func NAME` | `detector` | Function name to invoke. Default: `detector_yolov8_wasm`. |
| `detector --max-output BYTES` | `detector` | Maximum output bytes. Default: `16777216`. |

#### Web Mode Options

| Option | Description |
|---------|-------------|
| `web --addr ADDR` | Web server bind address. Default: `127.0.0.1:8081`. |
| `web --url URL` | TAM URL used by `POST /teep`. Default: `http://localhost:8080/tam`. |
| `web --wapp NAME` | WASM app name used by Web install/detect flow. Default: `yolov8.wasm`. |
| `web --func NAME` | WASM function used by Web detect flow. Default: `detector_yolov8_wasm`. |
| `web --max-output BYTES` | Maximum detector output bytes in Web mode. Default: `16777216`. |

## Tests

Run unit tests using the test makefile:

```
host$ make -f Makefile.test
```

## Design Documents

You can access the design documents here:

- External Design: [`doc/ExternalDesignDocument.md`](./doc/ExternalDesignDocument.md)
- Internal Design: [`doc/InternalDesignDocument.md`](./doc/InternalDesignDocument.md)

# Acknowledgement

This work was supported by JST K Program Grant Number JPMJKP24U4, Japan.
