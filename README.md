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
Why Nexus-LOB?

Most introductory algorithmic-trading projects model a market using something similar to:

price → quantity

and then simulate execution by manually modifying arrays.

That is useful for demonstrating concepts, but it misses several properties that become important when studying real execution:

price-time priority
order identity
partial execution
cancellation
modification semantics
market-order sweeps
liquidity depletion
queue position
crossed-book prevention
deterministic replay
latency-sensitive data structures

Nexus-LOB treats the limit order book itself as a first-class systems component.

This creates a research stack where:

Market Data
     │
     ▼
ITCH Parser
     │
     ▼
L2 Replay Engine
     │
     ▼
Order Book State
     │
     ├───────────────┐
     ▼               ▼
C++ Matching     Python Stub
Engine            Reference
     │               │
     └───────┬───────┘
             ▼
      Execution Environment
             │
       ┌─────┴─────┐
       ▼           ▼
   Baselines       RL
       │           │
       └─────┬─────┘
             ▼
    Execution Analytics

The resulting system is intended to bridge the gap between:

market microstructure research

and

performance-oriented trading systems engineering.

Core Objectives

Nexus-LOB is being developed around five major subsystems.

Subsystem	Purpose	Status
C++ Matching Engine	Deterministic low-latency order matching	Implemented
Python Quant Layer	Market simulation and execution research	Implemented
RL Execution Agent	Learn optimal execution policies	Planned
CUDA Risk Engine	Monte-Carlo VaR/CVaR acceleration	Planned
Zero-Copy Dashboard	Live order-book / execution telemetry	Partially implemented

The intended final architecture is:

                   ┌───────────────────────────────┐
                   │       Market Data / ITCH       │
                   └──────────────┬────────────────┘
                                  │
                                  ▼
                   ┌───────────────────────────────┐
                   │      Replay / L2 Builder       │
                   └──────────────┬────────────────┘
                                  │
                                  ▼
        ┌─────────────────────────────────────────────────┐
        │              Nexus Matching Layer                │
        │                                                  │
        │   C++ LimitOrderBook     Python StubOrderBook    │
        │          │                         │              │
        │          └───────── Same Contract ─┘              │
        └───────────────────────┬─────────────────────────┘
                                │
                                ▼
                    ┌────────────────────────┐
                    │ Execution Environment  │
                    │      Gymnasium         │
                    └───────────┬────────────┘
                                │
                 ┌──────────────┼──────────────┐
                 ▼              ▼              ▼
              TWAP            VWAP            POV
                 │              │              │
                 └──────────────┼──────────────┘
                                │
                                ▼
                         RL Execution Agent
                         PPO / GRPO / etc.
                                │
                                ▼
                   Execution Analytics / Risk
Architecture
High-Level Architecture
┌───────────────────────────────────────────────────────────┐
│                     Nexus-LOB Platform                    │
├───────────────────────────────────────────────────────────┤
│                                                           │
│  Market Data                                             │
│  ┌──────────────┐      ┌───────────────┐                │
│  │ ITCH 5.0     │─────▶│ Replay Engine │                │
│  │ Parser       │      │ / L2 Builder  │                │
│  └──────────────┘      └───────┬───────┘                │
│                                │                         │
│                                ▼                         │
│                    ┌─────────────────────┐              │
│                    │ Frozen Book Contract │              │
│                    └──────────┬──────────┘              │
│                               │                         │
│                 ┌─────────────┴─────────────┐           │
│                 ▼                           ▼           │
│        ┌────────────────┐         ┌────────────────┐    │
│        │ C++ Engine     │         │ Python Stub    │    │
│        │                │         │                │    │
│        │ LimitOrderBook │         │ StubOrderBook  │    │
│        └───────┬────────┘         └───────┬────────┘    │
│                │                          │             │
│                └───────────┬──────────────┘             │
│                            ▼                            │
│                 ┌──────────────────────┐                │
│                 │ Book Adapter Layer   │                │
│                 └──────────┬───────────┘                │
│                            ▼                            │
│                 ┌──────────────────────┐                │
│                 │ Gymnasium Execution  │                │
│                 │ Environment          │                │
│                 └──────────┬───────────┘                │
│                            │                            │
│                 ┌──────────┼───────────┐                │
│                 ▼          ▼           ▼                │
│               TWAP       VWAP         POV               │
│                 │          │           │                │
│                 └──────────┼───────────┘                │
│                            ▼                            │
│                     RL Execution                        │
│                            │                            │
│                            ▼                            │
│                    Risk / Analytics                      │
│                                                           │
└───────────────────────────────────────────────────────────┘
