# Server Dependency Network Analyzer

A graph analysis tool designed for systems administrators to map, query, and modify directed communication and dependency relationships between infrastructure servers.

## Features
- **Adjacency Matrix & Vertex Mapping:** Represents services as graph vertices and dependencies as directed weighted/binary edges inside a 2D matrix.
- **Dynamic Graph Updates:** Supports adding/removing servers and creating/deleting dependencies on the fly. When a server is removed, all associated incoming and outgoing edges are cleaned up automatically.
- **Dependency Queries:** Inspects both direct outgoing dependencies (services a server relies on) and incoming dependencies (services depending on a server).
- **Reliability:** Gracefully handles duplicate insertions, non-existent entity removals, and out-of-bounds queries.

