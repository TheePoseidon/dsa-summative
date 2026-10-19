# Emergency Medical Supply Routing

A logistics and routing utility for regional health organizations to compute the shortest delivery path between medical supply warehouses, distribution centers, and health facilities.

## Features
- **Weighted Graph Network:** Models roads as weighted edges representing distance in kilometers.
- **Dijkstra's Shortest Path Algorithm:** Computes true minimum-distance routes rather than simply counting the fewest roads.
- **Path Reconstruction:** Maintains tracking structures during execution to reconstruct and display the exact sequence of facilities visited.
- **Input Validation:** Gracefully handles non-existent locations, identical source/destination selections, and unreachable target facilities without crashing.
