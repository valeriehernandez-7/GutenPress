# GutenPress

**Huffman-based file compressor/decompressor in C — serial, `fork()` + IPC and `pthread` + shared-memory versions, with a GTK3 comparison GUI.**



## Demo

[![GutenPress Demo](https://img.shields.io/badge/▶%20Watch-Demo%20Video-red?style=for-the-badge)](VIDEO_URL_HERE)

> Video walkthrough: compressing and decompressing the Project Gutenberg corpus with each version and the comparative analysis from the GUI.



## About

GutenPress compresses an entire directory of text files into a single `.huff` archive using **Huffman coding**, and restores it back to a directory with **MD5 integrity verification** of every file.

The same algorithm is implemented in three versions to compare how different concurrency models affect performance:

| Version | Concurrency model | Synchronization / communication |
|---|---|---|
| `serial` | Single process, single thread | — |
| `fork_ipc` | Worker pool of child processes created with `fork()` (one per CPU core) | Anonymous pipes, multiplexed with `poll()` to avoid pipe-buffer deadlocks |
| `pthreads` | Worker pool of POSIX threads (one per CPU core) | Shared address space, `pthread_mutex_t` protecting shared counters |

All three versions share the same core modules and produce a **cross-compatible archive format**: an archive created by any version can be extracted by any other.

A **GTK3 desktop application** runs the three compressor/decompressor pairs as subprocesses, times each one identically (`clock_gettime(CLOCK_MONOTONIC)`), and displays comparative statistics so the fastest method can be identified.

### Key features

- Canonical Huffman coding with an O(1) table-driven decoder
- MD5 checksum computed before compression and verified after decompression
- Static work partitioning across `sysconf(_SC_NPROCESSORS_ONLN)` workers
- Validated end-to-end against a 100 [Project Gutenberg's plaintext books](https://www.gutenberg.org/browse/scores/top#books-last30) (100/100 files MD5-verified, byte-identical `diff -rq`)


## Repo structure

```
GutenPress/
├── docs/
│   ├── IC6600-Proyecto1-ValerieMHernandezFernandez/   # LaTeX report
│   └── requirements.pdf                               # Project specification
├── src/
│   ├── common/                           # Shared modules used by every version
│   │   ├── bit_stream.c/.h                 # Bit-level writer/reader for Huffman codes
│   │   ├── compression_pipeline.c/.h       # Per-file compress/decompress flow (MD5 → Huffman → entry)
│   │   ├── directory_utils.c/.h            # Output directory creation and path helpers
│   │   ├── file_format.c/.h                # .huff archive header and entry header layout + I/O
│   │   ├── file_scanner.c/.h               # Lists regular files in a directory and reads them
│   │   ├── growable_buffer.c/.h            # Dynamic in-memory byte buffer (per worker output)
│   │   ├── huffman.c/.h                    # Frequencies, tree, canonical codes, table-driven decoder
│   │   ├── md5_utils.c/.h                  # MD5 hashing via OpenSSL EVP API
│   │   ├── min_heap.c/.h                   # Priority queue for Huffman tree construction
│   │   ├── work_partition.c/.h             # Static split of the file list across workers
│   │   ├── test_*.c                        # Smoke tests for each module (make tests)
│   │   └── Makefile
│   ├── fork_ipc/                         # Parallel version: processes + IPC
│   │   ├── fork_compressor.c               # fork() worker pool, results sent back through pipes + poll()
│   │   ├── fork_decompressor.c             # fork() workers decode, write and verify their entries
│   │   └── Makefile
│   ├── gui/                              # GTK3 desktop application
│   │   ├── icons/                          # SVG icon set (Tabler Icons)
│   │   ├── main.c                          # Window, 2x2 main menu and GtkStack navigation
│   │   ├── panel_compress.c/.h             # Compress a directory with the selected version
│   │   ├── panel_decompress.c/.h           # Extract an archive with the selected version
│   │   ├── panel_compare_compress.c/.h     # Runs the 3 compressors and compares results
│   │   ├── panel_compare_decompress.c/.h   # Runs the 3 decompressors and compares results
│   │   ├── process_runner.c/.h             # Runs a binary as a subprocess and times it (CLOCK_MONOTONIC)
│   │   ├── style.css                       # GTK CSS theme (color palette, typography)
│   │   ├── test_process_runner.c           # Smoke test for process_runner
│   │   └── Makefile
│   ├── gutenberg_corpus/                 # 100 Project Gutenberg books (.txt) used as test input
│   ├── pthreads/                         # Parallel version: threads + shared memory
│   │   ├── pthread_compressor.c            # pthread worker pool, mutex-protected progress counter
│   │   ├── pthread_decompressor.c          # Threads decode and write entries, shared counters under mutex
│   │   └── Makefile
│   ├── serial/                           # Sequential version (baseline)
│   │   ├── serial_compressor.c
│   │   ├── serial_decompressor.c
│   │   └── Makefile
│   └── tools/
│       └── gutenberg_downloader/
│           ├── download_gutenberg_corpus.py                            # Downloads the corpus
│           └── the_project_gutenberg-top_100_ebooks_last_30_days.html  # Saved top-100 list used as source
├── .gitignore
├── Makefile                              # Top-level build: delegates to each module's Makefile
└── README.md
```


## Development environment

| Component | Version |
|---|---|
| OS | Debian 13.6.0 "Trixie" (64-bit, GNOME) |
| Virtualization | Oracle VirtualBox on a Windows 10 Pro host (4 vCPUs, ~8 GB RAM) |
| Compiler | GCC 14.2.0 (`-std=c11`) |
| Build tools | GNU Make, `pkg-config` |
| GUI toolkit | GTK 3.24.49 |
| Cryptography | OpenSSL 3.5.7 (`libcrypto`, EVP API for MD5) |
| System APIs | POSIX (`fork`, `pipe`, `poll`, `pthread`) |
| Editor | Visual Studio Code (C/C++, Makefile Tools, LaTeX Workshop) |

Install the dependencies on Debian:

```bash
sudo apt update
sudo apt install build-essential pkg-config libgtk-3-dev libssl-dev
```


## Installation / Use

### 1. Clone the repository

```bash
git clone https://github.com/valeriehernandez-7/GutenPress.git
cd GutenPress
```

### 2. Build

From the repository root:

```bash
make
```

This builds the three versions and the GUI. Other targets:

| Command | Description |
|---|---|
| `make versions` | Build `serial`, `fork_ipc` and `pthreads` only |
| `make serial` | Build a single version (also `fork_ipc`, `pthreads`) |
| `make gui` | Build the GUI (builds the versions first) |
| `make tests` | Build the smoke tests in `src/common` |
| `make check` | Round-trip test of every version on `src/gutenberg_corpus` |
| `make run` | Build everything and launch the GUI |
| `make clean` | Remove compiled binaries |
| `make distclean` | Remove binaries and test artifacts |
| `make help` | List all targets |

### 3. Verify the build

```bash
make check
```

Each version compresses `src/gutenberg_corpus`, extracts the archive into `test_output/` and compares the result against the original with `diff -rq`. The check stops at the first difference. To test a different dataset:

```bash
make check CORPUS_DIR=path/to/other/texts
```

### 4. Command-line use

Each version provides a compressor (directory → `.huff` archive) and a decompressor (`.huff` archive → directory):

```bash
# Serial
./src/serial/serial_compressor       <input_directory> <output_archive.huff>
./src/serial/serial_decompressor     <input_archive.huff> <output_directory>

# fork() + IPC
./src/fork_ipc/fork_compressor       <input_directory> <output_archive.huff>
./src/fork_ipc/fork_decompressor     <input_archive.huff> <output_directory>

# pthreads
./src/pthreads/pthread_compressor    <input_directory> <output_archive.huff>
./src/pthreads/pthread_decompressor  <input_archive.huff> <output_directory>
```

Example with the included corpus:

```bash
./src/serial/serial_compressor src/gutenberg_corpus corpus.huff
./src/serial/serial_decompressor corpus.huff restored_corpus
diff -rq src/gutenberg_corpus restored_corpus && echo "Identical"
```

### 5. GUI

```bash
make run
```

The main menu offers four panels:

- **Comprimir** — compress a directory with the selected version
- **Descomprimir** — extract an archive with the selected version
- **Analizar Compresión** — run all three compressors and compare results
- **Analizar Descompresión** — run all three decompressors and compare results


## Contributors

- [Valerie M. Hernández Fernández](https://github.com/valeriehernandez-7)


## Academic information

| | |
|---|---|
| **University** | [Costa Rica Institute of Technology](https://www.tec.ac.cr/) |
| **Site** | [San José Local Technological Campus](https://www.tec.ac.cr/campus-tecnologico-local-san-jose) |
| **Degree** | [Computer Engineering](https://www.tec.ac.cr/ingenieria-computacion) |
| **Course** | [IC6600 - Operating System Principles](https://www.tec.ac.cr/plan-bachillerato-ingenieria-computacion) |
| **Assignment** | Project |
| **Semester** | II 2026 |