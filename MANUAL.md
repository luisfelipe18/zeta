# Zeta Language Manual

> Version 0.1.0-alpha · `zc` compiler with LLVM/clang backend

---

## Table of Contents

1. [Philosophy](#1-philosophy)
2. [Installation & Build](#2-installation--build)
3. [Hello, world](#3-hello-world)
4. [Variables and Types](#4-variables-and-types)
5. [Functions](#5-functions)
6. [Control Flow](#6-control-flow)
7. [Ranges](#7-ranges)
8. [Structs and impl](#8-structs-and-impl)
9. [String Interpolation](#9-string-interpolation)
10. [Manual Memory and Pointers](#10-manual-memory-and-pointers)
11. [Arrays and List Comprehensions](#11-arrays-and-list-comprehensions)
12. [Closures](#12-closures)
13. [Compiler Reference](#13-compiler-reference)
14. [Performance Guide](#14-performance-guide)

---

## 1. Philosophy

Z was born from a simple question: *why do we have to choose between speed and elegance?*

C is fast but austere. Python is beautiful but slow. Rust is powerful but complex. Z aims for the point where all three meet.

### The four principles

**1. Readable first.**
Code is read far more often than it is written. Z uses significant indentation (like Python), no braces `{}`, and no semicolons. The visual structure of a program mirrors its logical structure.

**2. Fast by default.**
Z compiles to LLVM IR and lets `clang -O3` do the heavy lifting: automatic vectorisation, aggressive inlining, dead-code elimination. No runtime, no garbage collector, no hidden cost.

**3. Explicit over implicit.**
Mutability must be declared (`mut`). Types are inferred when obvious but can always be annotated. Raw pointers are written as such, and only when the programmer wants them.

**4. The compiler is your ally.**
Type errors are reported clearly. `--ast` shows the syntax tree. `--emit-llvm` shows the generated IR. Z does not hide what it does.

---

## 2. Installation & Build

### Requirements

```
gcc     — to build the zc compiler itself
clang   — LLVM backend (default)
make    — orchestrates the build
```

### Build

```bash
git clone https://github.com/luisfelipe18/zeta.git
cd zeta
make          # produces the ./zc binary
```

### Use `zc` from anywhere

```bash
# Option A: add the folder to PATH
export PATH="$PATH:/path/to/zeta"

# Option B: copy to a standard directory
sudo cp zc /usr/local/bin/
```

---

## 3. Hello, world

```z
def main():
    print("Hello, world!")
```

Compile and run:

```bash
zc hello.z -o hello
./hello
# Hello, world!
```

Z does not need imports for `print`. String-interpolation printing is part of the language.

---

## 4. Variables and Types

### `let` — immutable binding

```z
let x = 42            # inferred type: i32
let pi = 3.14159      # inferred type: f64
let name = "Zeta"     # inferred type: str
let active = true     # inferred type: bool
```

Once bound with `let`, a variable cannot be modified. Attempting to do so is a compile-time error.

### `mut` — mutable variable

```z
mut counter = 0
counter += 1    # OK
counter = 100   # OK
```

### Explicit type annotation

The syntax is `name: Type`.

```z
let version: f64 = 1.0
let limit: i32 = 1000
mut buffer: str = "start"
```

### Primitive types

| Type | Description | Example |
|---|---|---|
| `i32` | Signed 32-bit integer | `42`, `-7` |
| `f64` | 64-bit floating point | `3.14`, `-0.001` |
| `bool` | Boolean | `true`, `false` |
| `str` | UTF-8 string | `"hello"` |
| `*T` | Pointer to type `T` | `*i32`, `*f64` |
| `[T]` | Dynamic array of type `T` | `[i32]` |

### Operators

| Category | Operators |
|---|---|
| Arithmetic | `+  -  *  /  %` |
| Comparison | `==  !=  <  >  <=  >=` |
| Logical | `&&  \|\|  !` |
| Compound assignment | `+=  -=  *=  /=  %=` |

---

## 5. Functions

### Basic definition

```z
def greet(name: str):
    print("Hello, {name}!")
```

### With a return value

```z
def add(a: i32, b: i32) -> i32:
    return a + b
```

### Multiple parameters and logic

```z
def max(a: i32, b: i32) -> i32:
    if a > b:
        return a
    return b

def is_even(n: i32) -> bool:
    return n % 2 == 0
```

### Recursion

```z
def fibonacci(n: i32) -> i32:
    if n <= 1:
        return n
    return fibonacci(n - 1) + fibonacci(n - 2)
```

### Calling functions

```z
def main():
    let r = add(3, 4)
    print("3 + 4 = {r}")

    let m = max(10, 25)
    print("Max: {m}")
```

---

## 6. Control Flow

### `if` / `else`

```z
def classify(n: i32) -> str:
    if n < 0:
        return "negative"
    if n == 0:
        return "zero"
    return "positive"
```

```z
def main():
    let x = 15
    if x % 2 == 0:
        print("{x} is even")
    else:
        print("{x} is odd")
```

### `while`

```z
def count_up(n: i32):
    mut i = 1
    while i <= n:
        print("{i}")
        i += 1
```

```z
def sum_up_to(n: i32) -> i32:
    mut total = 0
    mut i = 1
    while i <= n:
        total += i
        i += 1
    return total
```

### `for` with range

See [Ranges](#7-ranges) for the full syntax.

```z
def main():
    for i in 0..5:
        print("i = {i}")
    # prints 0, 1, 2, 3, 4
```

---

## 7. Ranges

Z has two range operators that can be used in `for` loops:

### `..` — exclusive range (right end NOT included)

```z
for i in 0..10:
    # i takes values: 0, 1, 2, ..., 9
```

```z
def sum_zero_to_nine() -> i32:
    mut total = 0
    for i in 0..10:
        total += i
    return total          # 45
```

### `..=` — inclusive range (right end IS included)

```z
for i in 1..=100:
    # i takes values: 1, 2, 3, ..., 100
```

```z
def gauss_sum() -> i32:
    mut total = 0
    for i in 1..=100:
        total += i
    return total          # 5050
```

### Arbitrary start value

```z
for i in 5..=15:
    print("{i}")          # 5, 6, ..., 15
```

### Factorial with inclusive range

```z
def factorial(n: i32) -> i32:
    mut result = 1
    for i in 1..=n:
        result *= i
    return result

def main():
    print("10! = {factorial(10)}")    # 3628800
```

---

## 8. Structs and `impl`

### Defining a struct

```z
struct Point:
    x: f64
    y: f64
```

### Creating an instance (named fields)

```z
def main():
    let origin = Point(x=0.0, y=0.0)
    let p      = Point(x=3.0, y=4.0)
```

### Accessing fields

```z
def main():
    let p = Point(x=3.0, y=4.0)
    print("x={p.x}  y={p.y}")
```

### Methods with `impl`

Group a struct's methods in an `impl` block:

```z
struct Point:
    x: f64
    y: f64

impl Point:
    # immutable 'self' — read-only
    def distance_sq(self) -> f64:
        return self.x * self.x + self.y * self.y   # returns |p|²

    # 'mut self' — can modify fields
    def translate(mut self, dx: f64, dy: f64):
        self.x += dx
        self.y += dy

def main():
    mut p = Point(x=3.0, y=4.0)
    let d2 = p.distance_sq()
    print("|p|² = {d2}")             # 25.0
    p.translate(1.0, -1.0)
    print("New x={p.x} y={p.y}")    # x=4.0 y=3.0
```

### Nested structs

```z
struct Vector3:
    x: f64
    y: f64
    z: f64

impl Vector3:
    def scale(mut self, s: f64):
        self.x *= s
        self.y *= s
        self.z *= s

struct Particle:
    mass:     f64
    position: Vector3

def main():
    mut p = Particle(mass=1.0, position=Vector3(x=1.0, y=0.0, z=0.0))
    p.position.scale(10.0)
    let px = p.position.x
    print("pos.x = {px}")           # 10.0
```

---

## 9. String Interpolation

Z interpolates values directly inside string literals using `{expression}`:

```z
let name = "Zeta"
let version = 1
print("Welcome to {name} v{version}")
# Welcome to Zeta v1
```

Works with any type that has a text representation:

```z
let x: i32 = 42
let pi: f64 = 3.14159
let ok: bool = true

print("x={x}  pi={pi}  ok={ok}")
# x=42  pi=3.14159  ok=1
```

Struct field access inside interpolation:

```z
let p = Point(x=3.0, y=4.0)
print("Point at ({p.x}, {p.y})")
```

---

## 10. Manual Memory and Pointers

Z provides direct heap access for high-performance code, with no garbage collector.

### Pointer syntax

| Expression | Meaning |
|---|---|
| `*T` | Type "pointer to T" |
| `alloc(T)` | Allocate memory for one value of type T |
| `free(p)` | Release the memory pointed to by p |
| `ptr.*` | Dereference: read or write the pointed-to value |

### Full example

```z
def process():
    # Allocate an integer on the heap
    mut ptr: *i32 = alloc(i32)

    # Write via dereference
    ptr.* = 99

    # Read via dereference
    let val = ptr.*
    print("Value: {val}")   # Value: 99

    # Free (required without a borrow checker)
    free(ptr)
```

### Swapping two values

```z
def swap(a: *i32, b: *i32):
    let tmp = a.*
    a.* = b.*
    b.* = tmp

def main():
    mut x: *i32 = alloc(i32)
    mut y: *i32 = alloc(i32)
    x.* = 10
    y.* = 20
    swap(x, y)
    let vx = x.*
    let vy = y.*
    print("x={vx}  y={vy}")   # x=20  y=10
    free(x)
    free(y)
```

> **Design note:** A future version of Z will include a borrow checker that statically
> verifies pointer lifetimes, eliminating most memory bugs without explicit `free`.

---

## 11. Arrays and List Comprehensions

### Array literal

```z
let nums = [1, 2, 3, 4, 5]
```

### List comprehension

The syntax `[expr for var in iter]` generates a new array:

```z
let squares = [x*x for x in 0..6]
# [0, 1, 4, 9, 16, 25]
```

With a filter (`if`):

```z
let evens = [x for x in 0..20 if x % 2 == 0]
# [0, 2, 4, 6, 8, 10, 12, 14, 16, 18]
```

---

## 12. Closures

Closures use the syntax `|parameters| expression`:

```z
let twice   = |x| x * 2
let add     = |a, b| a + b
let negate  = |x| x * -1
```

They are especially useful combined with list comprehensions or higher-order functions (coming soon to the standard library).

---

## 13. Compiler Reference

### Main flags

```
zc <file.z>                  Compile → native binary (LLVM/clang -O3)
zc <file.z> -o output        Explicit output name
zc <file.z> --emit-llvm      Print LLVM IR to stdout and exit
zc <file.z> --emit-c         Print generated C to stdout and exit
zc <file.z> --ast            Print the AST and exit
zc <file.z> --backend=c      Use C transpiler + gcc instead of LLVM
```

### LLVM backend (default)

The default pipeline is:

```
Z source → LLVM IR (.ll) → clang -O3 → native binary
```

`clang -O3` activates all LLVM optimisations: automatic vectorisation (SIMD),
inlining, dead-code elimination, loop unrolling, and more.

### C backend (debug / portability)

```bash
zc program.z --backend=c -o program
```

Pipeline:

```
Z source → generated C → gcc -O2 → native binary
```

Useful for debugging the compiler or on systems where `clang` is not available.

### Inspecting the generated LLVM IR

```bash
zc examples/primes.z --emit-llvm
```

```llvm
; Generated by zc LLVM backend
target triple = "arm64-apple-macosx26.0.0"

declare i32 @printf(i8*, ...)

define i1 @is_prime(i32 %n.param) {
entry:
  %p0 = alloca i32
  store i32 %n.param, i32* %p0
  ...
}
```

---

## 14. Performance Guide

### General rules

1. **Prefer `let` wherever possible.** Immutable variables allow the compiler to make more assumptions.

2. **Use `for i in a..b` ranges instead of `while`.** They compile identically, but the intent is clearer.

3. **Avoid strings in tight loops.** String interpolation calls `printf`. In loops with millions of iterations, work with `i32` or `f64` directly.

4. **The LLVM backend is always the fastest.** `clang -O3` applies passes that `gcc -O2` does not perform by default. Use `--backend=c` only for debugging.

5. **Pointers are for systems code.** For numerical algorithms, stack variables (`let`/`mut`) are equally fast and safer.

### Backend comparison

| Scenario | Recommended backend |
|---|---|
| Production, maximum performance | LLVM (default) |
| Compiler debugging | `--backend=c` + `--emit-c` |
| Portability to systems without clang | `--backend=c` |
| Inspecting generated code | `--emit-llvm` or `--emit-c` |

### Running your own benchmark

```bash
# Compile your Z program
zc my_program.z -o my_prog_z

# Compile the C equivalent for comparison
gcc -O2 my_program.c -o my_prog_c

# Measure
time ./my_prog_z
time ./my_prog_c
```

---

*Manual for Z 0.1.0-alpha. The language is under active development — some features are still being implemented.*
