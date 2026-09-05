Nexus-LOB
Low-Latency Limit Order Book • Execution Simulation • Market Microstructure • Quantitative Trading Infrastructure

<p align="center">

A hybrid C++/Python quantitative trading research platform built around a deterministic, low-latency limit-order-book matching engine.

<br />









</p>

Table of Contents
Overview
Why Nexus-LOB?
Core Objectives
Architecture
System Components
1. C++ Matching Engine
2. Cross-Language State Contract
3. Python Quant Layer
4. ITCH 5.0 Replay
5. Execution Environment
6. Execution Baselines
7. Shared-Memory Transport
8. Python/C++ Bridge
Order Book Model
Order Semantics
Execution Environment
Reward Design
Data & Replay
Testing & Verification
Current Validation
Performance Philosophy
Repository Structure
Requirements
Installation
Building the C++ Engine
Building the Python Extension
Running Tests
Running the Shared-Memory Demo
Using the Matching Engine
Using the Python Environment
Design Decisions
Correctness Guarantees
Roadmap
Research Directions
Limitations
Project Status
Contributing
License
Author
Overview

Nexus-LOB is a quantitative trading infrastructure project that combines a low-latency C++ limit-order-book engine with a Python-based market-microstructure and execution-research stack.

The system is designed around a simple principle:

The simulation used to research an execution strategy should behave like a real matching engine, not like a simplified price-array toy model.

The project therefore separates latency-sensitive exchange mechanics from research-oriented Python components.

The core matching engine is implemented in modern C++ and supports:

Limit orders
Market orders
IOC orders
FOK orders
GTC orders
Order cancellation
Order modification
Price-time priority
Partial fills
Multi-level book sweeps
Deterministic integer-tick pricing
Fixed-capacity order storage
O(1)-style level lookup and order-ID lookup
Zero-allocation order-pool architecture

The Python layer builds on top of the same market state representation to provide:

Market-data replay
ITCH 5.0 parsing
L2 order-book reconstruction
Execution simulation
Gymnasium-compatible reinforcement-learning environments
Implementation-shortfall rewards
TWAP
VWAP
POV
Passive execution baselines
C++ engine adapters
A pure-Python reference/stub order book
Cross-language parity testing

The architecture is deliberately designed so that the Python research stack can be developed against a deterministic StubOrderBook, then switched to the real C++ engine without changing the surrounding execution logic.
