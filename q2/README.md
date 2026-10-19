# Network Device Command Registry

A command verification and audit tool for network operations teams managing embedded routers, gateways, and industrial controllers.

## Features
- **Binary Search Tree (BST):** Loads approved commands from a text file (`commands.txt`) and stores them lexicographically for $O(\log N)$ search efficiency.
- **Smart Similarity Engine:** Uses **Levenshtein Distance** to suggest corrections when an unknown command is entered (e.g., *Did you mean: SHOW_STATUS?*).
- **Audit Logging:** Automatically logs unknown command attempts to `audit.log` along with exact timestamps (`YYYY-MM-DD HH:MM:SS`), while successfully verified commands bypass audit logging.
- **Resource Constraints:** Enforces a hard limit of **50 commands** and properly manages dynamic memory allocation/deallocation.
