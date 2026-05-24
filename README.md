<div align="center">
  <img src="assets/logo.svg" alt="ζ — The Zeta Language" width="160"/>

  <h1>ζ &nbsp;—&nbsp; The Zeta Language</h1>

  <p><strong>Python simplicity &nbsp;·&nbsp; C performance &nbsp;·&nbsp; Rust safety</strong></p>

  <p>
    <img alt="Build" src="https://img.shields.io/badge/build-passing-22c55e?style=flat-square"/>
    <img alt="Version" src="https://img.shields.io/badge/version-0.1.0--alpha-f97316?style=flat-square"/>
    <img alt="Backend" src="https://img.shields.io/badge/backend-LLVM%20%2F%20clang--O3-818cf8?style=flat-square"/>
    <img alt="License" src="https://img.shields.io/badge/license-MIT-38bdf8?style=flat-square"/>
  </p>
</div>

---

**Z** is a compiled, statically-typed language that generates LLVM IR and compiles with `clang -O3`. It is designed to be as readable as Python, as fast as C, and to feel natural for systems programming, data science, and AI workloads.

```z
def is_prime(n: i32) -> bool:
    if n < 2: return false
    if n == 2: return true
    if n % 2 == 0: return false
    mut i = 3
    while i * i <= n:
        if n % i == 0: return false
        i += 2
    return true

def main():
    mut count = 0
    mut n = 2
    while n <= 1000:
        if is_prime(n): count += 1
        n += 1
    print("Primes up to 1000: {count}")
```

---

## ⚡ Benchmarks

> Prime-counting algorithm, N = 500 000, average of 20 runs — Apple Silicon (arm64)

| Language | Avg time | Compiler / runtime |
|---|---|---|
| **Z** | **~0.009 s** | `zc` → `clang -O3` |
| C | ~0.009 s | `gcc -O2` |
| Rust | ~0.010 s | `rustc -C opt-level=3` |
| Python 3 | ~1.100 s | CPython 3.x |

Z compiles to native LLVM IR, achieving performance on par with C and Rust — and **~120× faster than Python** on this workload.

Reproduce with:
```bash
make && cd bench && ./run.sh 500000
```

---

## ✨ Language Highlights

| Feature | Z syntax |
|---|---|
| Immutable binding | `let x = 42` |
| Mutable variable | `mut count = 0` |
| Typed binding | `let pi: f64 = 3.14159` |
| Function | `def add(a: i32, b: i32) -> i32:` |
| Struct | `struct Point: x: f64  y: f64` |
| Methods | `impl Point: def dist(self) -> f64:` |
| Range (exclusive) | `for i in 0..10:` |
| Range (inclusive) | `for i in 1..=100:` |
| String interpolation | `print("Result: {value}")` |
| Raw pointer | `mut p: *i32 = alloc(i32)` |
| List comprehension | `[x*x for x in 0..10]` |
| Closure | `\|x\| x * 2` |

---

## 🚀 Getting Started

### Requirements

| Tool | Purpose |
|---|---|
| `gcc` | Builds the `zc` compiler itself |
| `clang` | Compiles the generated LLVM IR (default backend) |
| `make` | Orchestrates the build |

### Build

```bash
git clone https://github.com/luisfelipe18/zeta.git
cd zeta
make
```

This produces the `zc` binary in the project root.

### Your first Z program

```bash
cat examples/hello.z
```
```z
def main():
    let name = "world"
    print("Hello, {name}!")
```
```bash
./zc examples/hello.z -o hello
./hello
# Hello, world!
```

---

## 🛠 Compiler Usage

```
zc <file.z> [-o output]    compile → binary  (LLVM/clang -O3, default)
zc <file.z> --emit-llvm    print LLVM IR and exit
zc <file.z> --emit-c       print generated C and exit  (debug)
zc <file.z> --ast          print AST and exit
zc <file.z> --backend=c    use C transpiler + gcc instead of LLVM
```

---

## 📁 Project Structure

```
zeta/
├── lexer.c / lexer.h        Tokenizer (indent-aware)
├── parser.c / parser.h      Recursive-descent parser → AST
├── analyzer.c / analyzer.h  Semantic analysis & type inference
├── codegen.c / codegen.h    C backend (transpiler, --backend=c)
├── llvmgen.c / llvmgen.h    LLVM IR backend (default)
├── zc_main.c                Compiler driver
├── main.c                   z_test AST viewer (dev tool)
├── Makefile
├── examples/
│   ├── hello.z              Hello, world
│   ├── primes.z             Prime-counting benchmark
│   ├── fibonacci.z          Fibonacci (recursive + iterative)
│   ├── structs.z            Structs and methods
│   ├── ranges.z             Range operators and accumulators
│   ├── list_ops.z           List comprehensions
│   └── pointers.z           Manual memory management
├── bench/
│   ├── run.sh               Benchmark runner
│   ├── prime.c / .py / .rs  Reference implementations
├── editors/
│   └── jetbrains/
│       ├── README.md              Installation instructions
│       └── zeta-lang.tmbundle/    TextMate grammar (JetBrains / VS Code / Sublime)
└── assets/
    └── logo.svg
```

---

## 📖 Documentation

See **[MANUAL.md](MANUAL.md)** for the full language reference, including:

- Type system (`i32`, `f64`, `bool`, `str`, `*T`, `[T]`)
- Control flow (`if`, `while`, `for`, range syntax)
- Structs and `impl` blocks
- String interpolation
- Manual memory management (`alloc` / `free`)
- List comprehensions and closures
- Compiler flags reference

---

## 🗺 Roadmap

| Phase | Status | Description |
|---|---|---|
| 1 — Lexer | ✅ | Indent-aware tokenizer, full operator set |
| 2 — Parser | ✅ | Recursive-descent, full AST |
| 3 — Analyzer | ✅ | Type inference, semantic validation |
| 4a — C backend | ✅ | Transpiler to C → gcc |
| 4b — LLVM backend | ✅ | Direct LLVM IR → clang -O3 |
| 5 — Borrow checker | 🔜 | Ownership rules + safety guarantees |
| 6 — Generics | 🔜 | Parametric polymorphism |
| 7 — Pattern matching | 🔜 | `match` / `case` expressions |
| 8 — Bootstrapping | 🔜 | Z compiler written in Z |

---

## 📜 License

MIT — see [LICENSE](LICENSE) for details.

---

<div align="center">
  <sub>Built with ❤️ and a lot of LLVM IR</sub>
</div>
