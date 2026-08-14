## Contributors

### Author:

<table>
  <tr>
    <td align="center">
      <a href="https://github.com/Dzmitry-kulik">
        <img src="https://github.com/Dzmitry-kulik.png" width="100px;" alt="Dzmitry Kulik"/><br />
        <sub><b>Dzmitry Kulik</b></sub>
      </a>
    </td>
  </tr>
</table>

### Mentor:

<table>
  <tr>
    <td align="center">
      <a href="https://github.com/MrDaila007">
        <img src="https://github.com/MrDaila007.png" width="100px;" alt="Danila Surok"/><br />
        <sub><b>Danila Surok</b></sub>
      </a>
    </td>
  </tr>
</table>

# STM32F411CE6 UART driver

![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![Build](https://img.shields.io/badge/CMake-3.22%2B-green.svg)
![Target](https://img.shields.io/badge/Microcontroller-STM32F411CE6-orange.svg)
![Docker](https://img.shields.io/badge/Environment-Docker-blue.svg)
![CI/CD](https://img.shields.io/badge/CI%2FCD-GitHub%20Actions-blueviolet.svg)
![Emulator](https://img.shields.io/badge/Testing-Renode-brightgreen.svg)

A reliable asynchronous data transmission driver over UART (DMA) for the STM32F411CE6 microcontroller, featuring guaranteed frame delivery (Stop-and-Wait ARQ), automatic data integrity verification using CRC16-CCITT, and a custom finite-state machine (FSM) parser.

The project features a fully automated Docker-based CI/CD pipeline with automated firmware size checks, software emulation testing in Renode, benchmark plot generation, and artifact publishing.

---

## Key Features

* **Asynchronous Reception (DMA RX + IDLE):** Continuous reading of incoming bytes via a circular buffer without data loss and with minimal CPU overhead.
* **Asynchronous Transmission (DMA TX Manager):** Packet transmission queue supporting timeouts and automatic retransmissions (Retries).
* **FSM Frame Parser (Finite State Machine):** Non-blocking byte-by-byte frame assembly.
* **Data Integrity (CRC16):** Header and payload protection using the CRC16-CCITT algorithm (`0x1021`).
* **Guaranteed Delivery (Stop-and-Wait ARQ):** Delivery confirmation via ACK packets with timeout-based retransmissions (default: 150 ms, 3 retries).
* **Automated CI/CD & Renode Testing:** Continuous testing in GitHub Actions using a Dockerized Renode emulator, automatically generating execution benchmarks and size reports.

---

## Binary Protocol Structure

All data is transmitted in binary frames formatted as follows:

| Field | Size (Bytes) | Value | Description |
| :--- | :---: | :---: | :--- |
| **PREAMBLE** | 2 | `0xAA 0x55` | Frame start marker for FSM synchronization |
| **VERSION** | 1 | `0x01` | Protocol version |
| **MSG_TYPE** | 1 | `0x01` / `0x02` / ... | Frame type (`DATA`, `ACK`, `GET_STATS`, `STATS_RESP`) |
| **SEQ_NUM** | 1 | `0..255` | Incremental sequence number |
| **PAYLOAD_LEN** | 2 | Big-Endian (MSB, LSB) | Payload length in bytes |
| **PAYLOAD** | N | Bytes | Payload data (0 <= N <= 256) |
| **CRC16** | 2 | Big-Endian (MSB, LSB) | CCITT-FALSE checksum calculated over Header + Payload |

### Message Types (`MSG_TYPE`)
* `0x01` — **`DATA`**: Payload data frame.
* `0x02` — **`ACK`**: Delivery acknowledgment (`payload_len = 0`).
* `0x03` — **`GET_STATS`**: Request for internal STM32 diagnostic counters.
* `0x04` — **`STATS_RESP`**: Diagnostics response containing error statistics and frame counters.

---

## Building & Testing via Docker

The project utilizes a multi-stage Docker build environment (with support for build caching via Docker Buildx) to ensure reproducible compilation, Renode emulation testing, and artifact extraction.

### Requirements
* **Docker Engine** >= 20.10 (with Buildx support)

### Local Build (Replicating CI/CD Pipeline)

To compile the firmware, execute Renode tests, and export artifacts (`UART_DRIVER.elf` and `benchmark_results.png`) to your local `./build_out` directory:

```bash
docker buildx build \
  --target artifacts \
  --output type=local,dest=./build_out .
```

After compilation finishes, all output artifacts will appear in `./build_out/`:
* `build_out/UART_DRIVER.elf` — Target microcontroller binary.
* `build_out/benchmark_results.png` — Generated execution benchmark graph from Renode emulation.

---

## CI/CD Pipeline Workflow

The repository includes an automated GitHub Actions workflow (`.github/workflows/main.yml`) that triggers on every `push` and `pull_request` to `main`/`master` branches:

1. **Source Code Sanitization:** Runs a pre-build hook stripping single-line C/C++ comments (`//`).
2. **GHCR Authentication:** Logins to GitHub Container Registry to pull cached base toolchain images.
3. **Multi-Stage Docker Build:**
   * Compiles the firmware using ARM GCC Toolchain.
   * Performs binary size inspection.
   * Runs hardware emulation and integration tests inside Renode.
4. **Artifact Generation & Publishing:** Exports `UART_DRIVER.elf` and `benchmark_results.png`, attaching them as downloadable pipeline artifacts retained for 7 days.
