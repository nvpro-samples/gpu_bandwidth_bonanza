# GPU bandwidth bonanza

gpu_bandwidth_bonanza is a command-line utility designed to benchmark and analyze memory transfer speeds on systems with one or more GPUs. It provides a comprehensive overview of memory transfer speed performance across different graphics APIs (Vulkan, D3D12, and CUDA) and various transfer paths, helping developers and system administrators identify performance characteristics and potential bottlenecks.

## Features
- Multi-API Benchmarking: Simultaneously tests memory transfer speed using Vulkan, D3D12, and CUDA.
- Comprehensive Transfer Analysis: Measures speeds for various memory operations:
- Host ↔ Device: System RAM to/from GPU VRAM.
- Intra-Device: Transfers within a single GPU's VRAM.
- Inter-Device (P2P): Transfers between different GPUs over interconnects like NVLink or PCIe.
- Detailed System Enumeration: Detects and displays information about available GPUs under each API, including UUIDs, LUIDs, and NVLink status.
- Rich Data Visualization: Presents results in several user-friendly formats:
- An intuitive ASCII bar chart for quick visual comparisons.
- Clear summary tables (matrices) for each API.
- Symmetry Analysis: Calculates a "transfer speed symmetry rating" to quantify how balanced the performance is between GPUs.

## Usage
`gpu_bandwidth_bonanza --help | -h`
`gpu_bandwidth_bonanza --version | -v`
`gpu_bandwidth_bonanza [--vulkan-device-group <index> | --dxgi-adapter <index>] [--no-vulkan] [--no-cuda] [--no-d3d12] [--duration <millis>] [--output <path>]`

### Options
|Option                       |Effect|
|-----------------------------|------|
|--help, -h                   |Shows this text|
|--version, -v                |Shows version and build information|
|--vulkan-device-group <index>|Selects the GPUs by their Vulkan device group|
|--dxgi-adapter <index>       |Selects the GPUs by their DXGI adapter|
|--no-vulkan                  |Disables Vulkan|
|--no-cuda                    |Disables CUDA and NVML|
|--no-d3d12                   |Disables D3D12|
|--print-vulkan-mem-props     |Prints memory properties for each physical Vulkan device|
|--duration <millis>          |Sets the measuring period in milliseconds per transfer; default: 1000|
|--output <path>              |Writes the results to a csv file|

## Understanding the Output
The output is organized into several sections for clarity.

1. API Enumeration
The initial sections (Vulkan, D3D12, CUDA, NVML) list all compatible graphics devices found on the system. The tool indicates which devices are selected for the benchmark. This part is useful for verifying that your hardware is correctly detected and for inspecting properties like UUIDs, LUIDs, and the status of NVLink bridges.

2. Benchmarks
This is a flat list of the raw transfer speed measurements for every tested transfer path, reported in gigabytes per second (GiB/s).

3. Results
This final section provides a summarized and visualized analysis of the benchmark data.

### ASCII Chart
A bar chart that visually compares the most critical transfer speeds (GPU-to-GPU and GPU-to-Host) across the three tested APIs. It's great for an at-a-glance performance comparison.

### Transfer Speed Matrices
These tables provide a complete overview of the transfer performance between all pairs of resources (host, dev 0, dev 1). You can quickly find the transfer speed from any source (row) to any destination (column).

### Transfer Speed Symmetry Rating
This metric indicates how uniform the performance is between corresponding paths (e.g., dev 0 ⟶ dev 1 vs. dev 1 ⟶ dev 0). A score closer to zero is better, signifying a more balanced or symmetrical system. A high score suggests an asymmetry that might be worth investigating.