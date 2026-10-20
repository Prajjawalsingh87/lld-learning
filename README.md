# lld-learning

[![Build](https://github.com/Prajjawalsingh87/lld-learning/actions/workflows/build.yml/badge.svg)](https://github.com/Prajjawalsingh87/lld-learning/actions/workflows/build.yml)

My notes and C++ examples for **Low-Level Design (LLD)**, starting with object-oriented programming fundamentals and building toward full design problems.

Each example is a small, self-contained C++ program that you can compile and run on its own.

## Index

### OOP fundamentals

| Topic | File | Key concept |
|---|---|---|
| Class and object | [oops_fundamentals/class_and_object.cpp](oops_fundamentals/class_and_object.cpp) | An `Order` class bundles private state (customer, items, total, placed flag) with the methods that change it. It also shows a constructor, a destructor and simple state checks (no items can be added after the order is placed). |

### Design problems

Each problem has its own folder with a README (requirements, mermaid class diagram, design patterns, complexity), a header-only implementation and a `*_test.cpp` test executable.

| Problem | Folder | Key concepts |
|---|---|---|
| LRU cache | [lru_cache/](lru_cache/) | Generic `LRUCache<K, V>` with O(1) `get`/`put`: a hash map pointing into a doubly linked list, using `splice` to move a node to the front. |
| Parking lot | [parking_lot/](parking_lot/) | Levels, spots sized for vehicle types, best-fit allocation, entry tickets and exit receipts, and fees through the **Strategy** pattern (hourly or flat, swappable at runtime). |
| Rate limiter | [rate_limiter/](rate_limiter/) | Token bucket and sliding window log behind one `RateLimiter` interface, per-client state, thread-safe with `std::mutex`, and an injected clock for deterministic tests. |
| Elevator system | [elevator_system/](elevator_system/) | Request queue, dispatcher cost function, SCAN scheduling, a per-car state machine (Idle, MovingUp, MovingDown, DoorsOpen) and a `step()` simulation. |

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

To build and run every test (any file named `*_test.cpp`):

```bash
mkdir -p build
failed=0
for f in $(find . -name '*_test.cpp' -not -path './.git/*'); do
  bin="build/$(basename "$f" .cpp)"
  g++ -std=c++17 -Wall "$f" -o "$bin" && "$bin" || { echo "FAILED: $f"; failed=1; }
done
[ "$failed" -eq 0 ]
```

Tests use [testing/mini_test.hpp](testing/mini_test.hpp), a tiny header-only framework in this repo, so nothing is downloaded at build time.

GitHub Actions ([build.yml](.github/workflows/build.yml)) compiles every `.cpp` file on each push and pull request, then runs every `*_test.cpp` binary. A failing test fails the build.

## Adding a new example

1. Put it in a topic folder, for example `oops_fundamentals/` or `design_patterns/`, as a `snake_case.cpp` file.
2. Add a row to the index above with a one-line key concept.
3. If a file is intentionally partial (has no `main`), CI compiles it with `-c` only.
4. For a design problem, put the implementation in a header in its own folder, add a `<name>_test.cpp` that ends with `MINI_TEST_MAIN()`, and add a short README.
