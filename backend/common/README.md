# Backend Common

Shared C++ utilities will live here as the API Server and Judge Worker begin to share code.

Phase 1 keeps the API Server self-contained. Later phases should move cross-cutting pieces such as response formatting, logging, config loading, database adapters, Redis queue clients, and domain models into this directory.

