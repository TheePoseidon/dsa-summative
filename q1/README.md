# Embedded Device Event Logger

A robust C program designed for embedded industrial monitoring devices to collect, store, and navigate diagnostic events (temperature warnings, low-battery alerts, sensor failures, communication errors) under strict memory constraints.

## Features
- **Doubly Linked List Architecture:** Maintains bidirectional chronological navigation (`head`, `tail`, and `current` pointers).
- **Bounded Memory Constraint:** Strictly capped at a maximum of **25 events**. Automatically implements FIFO eviction (removing the oldest event) when a 26th event arrives.
- **Interactive Navigation:** Allows operators to move forward (`n`) to newer events, backward (`p`) to older events, view current event details (`c`), simulate new events (`a`), clear all logs (`x`), or save and terminate (`q`).
- **Memory Safety:** Dynamically allocates and releases memory nodes to prevent memory leaks and dangling pointers.