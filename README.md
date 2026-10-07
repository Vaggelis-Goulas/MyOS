# MyOS

MyOS is a custom operating system developed from the ground up as a systems-programming project.

The project is written primarily in **C and Assembly** and is built using a dedicated cross-compilation toolchain. The goal of MyOS is to explore the fundamental components involved in creating an operating system, from the boot process and low-level hardware interaction to kernel development and, eventually, a custom compiler toolchain.

> **Project status:** Active development

---

## Features

MyOS is currently under development. Its functionality will expand as development continues.

Current areas of development include:

* Custom boot process
* Low-level Assembly code
* C-based kernel development
* Cross-compilation
* Custom linker configuration
* Automated build system using GNU Make
* Low-level memory and hardware management
* Kernel architecture and system components

Future development will include additional operating-system functionality and integration with a custom compiler.

---

# Requirements

MyOS is currently intended to be built from a **Linux/WSL environment**.

You will need:

* Linux or WSL
* Git
* GNU Make
* An `i686-elf` cross-compiler
* QEMU
* Standard development utilities required by the build system

> The cross-compiler is **not included in this repository**. It must be installed separately.

---

# Installing the Cross-Compiler

MyOS requires an `i686-elf` cross-compiler rather than the host system's normal GCC compiler.

Verify that the cross-compiler is installed and available in your `PATH`:

```bash
i686-elf-gcc --version
```

You should also be able to run:

```bash
i686-elf-as --version
i686-elf-ld --version
i686-elf-objcopy --version
```

If these commands cannot be found, install an appropriate `i686-elf` cross-compilation toolchain before attempting to build MyOS.

---

# Getting the Source Code

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/MyOS.git
```

Enter the project directory:

```bash
cd MyOS
```

---

# Building MyOS

The project uses **GNU Make** as its build system.

From the root directory of the repository, run:

```bash
make
```

The Makefile will invoke the required cross-compilation tools and generate the necessary build artifacts.

Generated files are placed in the project's build/output directories and are **not stored in the Git repository**.

---

# Cleaning the Build

To remove generated build files:

```bash
make clean
```

This removes files generated during compilation and allows the project to be rebuilt from a clean state.

After cleaning, running:

```bash
make
```

will regenerate the required build artifacts.

---

# Running MyOS

MyOS is currently designed to be executed using **QEMU**, allowing the operating system to be tested without installing it on physical hardware.

After successfully building the project, run:

```bash
YOUR_QEMU_COMMAND_HERE
```

Replace the command above with the QEMU command used by the current version of the project.

For example, depending on the generated image format, the command may resemble:

```bash
qemu-system-i386 -kernel path/to/kernel
```

or:

```bash
qemu-system-i386 -cdrom path/to/MyOS.iso
```

**Use the command specified by the current Makefile/build configuration.**

---

# Project Structure

The project is organized into several components responsible for different parts of the operating system.

```text
MyOS/
│
├── boot/          # Bootloader and early startup code
├── kernel/        # Kernel source code
├── ...            # Additional system components
│
├── Makefile       # Build system
├── linker.ld      # Linker configuration
└── README.md      # Project documentation
```

The exact structure may change as the operating system develops.

---

# Development Philosophy

MyOS is being developed with an emphasis on understanding the underlying mechanisms of an operating system rather than relying on high-level abstractions.

Important design decisions are documented throughout the project, including the reasoning behind architectural choices, implementation alternatives, and changes made during development.

The project is intended to evolve incrementally, with each subsystem being implemented and tested before additional complexity is introduced.

---

# Reproducible Builds

Generated binaries and intermediate compilation files are intentionally **not tracked by Git**.

Instead, the repository contains the source code and build configuration required to reproduce them.

A fresh checkout should therefore follow this general process:

```text
Clone repository
      ↓
Install required toolchain
      ↓
Run make
      ↓
Build artifacts generated
      ↓
Run MyOS with QEMU
```

This keeps the repository clean and ensures that the binaries correspond to the exact source code being built.

---

# Documentation

Additional technical documentation will cover the internal architecture and development of MyOS.

Topics may include:

* Boot process
* Kernel architecture
* Memory layout
* Cross-compilation
* Linker configuration
* Assembly/C interaction
* Hardware interaction
* Design decisions
* Implementation alternatives
* Performance considerations
* Compiler integration

---

# Contributing

MyOS is primarily a personal development project.

The repository is public so that the development process, source code, architecture, and technical decisions can be examined by anyone interested in operating-system development.

Suggestions, discussions, and improvements are welcome.

---

# License

[Add your chosen license here.]

---

## Author

Developed as an independent systems-programming project alongside university studies.

MyOS is intended as an ongoing exploration of:

**Operating Systems · Compilers · C · Assembly · Computer Architecture · Systems Programming**
# MyOS

MyOS is a custom operating system developed from the ground up as a systems-programming project.

The project is written primarily in **C and Assembly** and is built using a dedicated cross-compilation toolchain. The goal of MyOS is to explore the fundamental components involved in creating an operating system, from the boot process and low-level hardware interaction to kernel development and, eventually, a custom compiler toolchain.

> **Project status:** Active development

---

## Features

MyOS is currently under development. Its functionality will expand as development continues.

Current areas of development include:

* Custom boot process
* Low-level Assembly code
* C-based kernel development
* Cross-compilation
* Custom linker configuration
* Automated build system using GNU Make
* Low-level memory and hardware management
* Kernel architecture and system components

Future development will include additional operating-system functionality and integration with a custom compiler.

---

# Requirements

MyOS is currently intended to be built from a **Linux/WSL environment**.

You will need:

* Linux or WSL
* Git
* GNU Make
* An `i686-elf` cross-compiler
* QEMU
* Standard development utilities required by the build system

> The cross-compiler is **not included in this repository**. It must be installed separately.

---

# Installing the Cross-Compiler

MyOS requires an `i686-elf` cross-compiler rather than the host system's normal GCC compiler.

Verify that the cross-compiler is installed and available in your `PATH`:

```bash
i686-elf-gcc --version
```

You should also be able to run:

```bash
i686-elf-as --version
i686-elf-ld --version
i686-elf-objcopy --version
```

If these commands cannot be found, install an appropriate `i686-elf` cross-compilation toolchain before attempting to build MyOS.

---

# Getting the Source Code

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/MyOS.git
```

Enter the project directory:

```bash
cd MyOS
```

---

# Building MyOS

The project uses **GNU Make** as its build system.

From the root directory of the repository, run:

```bash
make
```

The Makefile will invoke the required cross-compilation tools and generate the necessary build artifacts.

Generated files are placed in the project's build/output directories and are **not stored in the Git repository**.

---

# Cleaning the Build

To remove generated build files:

```bash
make clean
```

This removes files generated during compilation and allows the project to be rebuilt from a clean state.

After cleaning, running:

```bash
make
```

will regenerate the required build artifacts.

---

# Running MyOS

MyOS is currently designed to be executed using **QEMU**, allowing the operating system to be tested without installing it on physical hardware.

After successfully building the project, run:

```bash
YOUR_QEMU_COMMAND_HERE
```

Replace the command above with the QEMU command used by the current version of the project.

For example, depending on the generated image format, the command may resemble:

```bash
qemu-system-i386 -kernel path/to/kernel
```

or:

```bash
qemu-system-i386 -cdrom path/to/MyOS.iso
```

**Use the command specified by the current Makefile/build configuration.**

---

# Project Structure

The project is organized into several components responsible for different parts of the operating system.

```text
MyOS/
│
├── boot/          # Bootloader and early startup code
├── kernel/        # Kernel source code
├── ...            # Additional system components
│
├── Makefile       # Build system
├── linker.ld      # Linker configuration
└── README.md      # Project documentation
```

The exact structure may change as the operating system develops.

---

# Development Philosophy

MyOS is being developed with an emphasis on understanding the underlying mechanisms of an operating system rather than relying on high-level abstractions.

Important design decisions are documented throughout the project, including the reasoning behind architectural choices, implementation alternatives, and changes made during development.

The project is intended to evolve incrementally, with each subsystem being implemented and tested before additional complexity is introduced.

---

# Reproducible Builds

Generated binaries and intermediate compilation files are intentionally **not tracked by Git**.

Instead, the repository contains the source code and build configuration required to reproduce them.

A fresh checkout should therefore follow this general process:

```text
Clone repository
      ↓
Install required toolchain
      ↓
Run make
      ↓
Build artifacts generated
      ↓
Run MyOS with QEMU
```

This keeps the repository clean and ensures that the binaries correspond to the exact source code being built.

---

# Documentation

Additional technical documentation will cover the internal architecture and development of MyOS.

Topics may include:

* Boot process
* Kernel architecture
* Memory layout
* Cross-compilation
* Linker configuration
* Assembly/C interaction
* Hardware interaction
* Design decisions
* Implementation alternatives
* Performance considerations
* Compiler integration

---

# Contributing

MyOS is primarily a personal development project.

The repository is public so that the development process, source code, architecture, and technical decisions can be examined by anyone interested in operating-system development.

Suggestions, discussions, and improvements are welcome.

## Author

Developed as an independent systems-programming project alongside university studies.

MyOS is intended as an ongoing exploration of:

**Operating Systems · Compilers · C · Assembly · Computer Architecture · Systems Programming**
