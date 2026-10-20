# lld-learning

[![Build](https://github.com/Prajjawalsingh87/lld-learning/actions/workflows/build.yml/badge.svg)](https://github.com/Prajjawalsingh87/lld-learning/actions/workflows/build.yml)

My notes and C++ examples for **Low-Level Design (LLD)**, starting with object-oriented programming fundamentals and building toward full design problems.

Each example is a small, self-contained C++ program that you can compile and run on its own.

## Index

### OOP fundamentals

| Topic | File | Key concept |
|---|---|---|
| Class and object | [oops_fundamentals/class_and_object.cpp](oops_fundamentals/class_and_object.cpp) | An `Order` class bundles private state (customer, items, total, placed flag) with the methods that change it. It also shows a constructor, a destructor and simple state checks (no items can be added after the order is placed). |

## Build and run

You need `g++` with C++17 support.

```bash
mkdir -p build
g++ -std=c++17 -Wall oops_fundamentals/class_and_object.cpp -o build/class_and_object
./build/class_and_object
```

Expected output:

```text
Item added: Burger
Item added: Fries
Customer name: Alice
Items:
- Burger
- Fries
Total amount: 230
Order placed: No
Order placed successfully.
Order object destroyed, resources released.
```

To compile every example at once:

```bash
mkdir -p build
failed=0
while read -r f; do
  g++ -std=c++17 -Wall "$f" -o "build/$(basename "$f" .cpp)" || { echo "FAILED: $f"; failed=1; }
done < <(find . -name '*.cpp' -not -path './.git/*')
[ "$failed" -eq 0 ]
```

GitHub Actions ([build.yml](.github/workflows/build.yml)) compiles every `.cpp` file on each push and pull request.

## Adding a new example

1. Put it in a topic folder, for example `oops_fundamentals/` or `design_patterns/`, as a `snake_case.cpp` file.
2. Add a row to the index above with a one-line key concept.
3. If a file is intentionally partial (has no `main`), CI compiles it with `-c` only.


## One-command local verification

Run `python3 scripts/build.py` from any directory. Requires Python 3 and GCC with
C++17 support. Set `CXX` to choose another GCC executable. Binaries keep their
source directory structure under `build/`, so exercises with the same filename
do not overwrite one another. Partial examples without `main` compile to objects.
The command compiles examples; it does not run programs that may wait for stdin.

These are native C++ exercises, not web applications; Vercel hosting is not applicable.
