DSA in C summative

# Project Report
## Q1: Embedded Device Event Logger

A monitoring device records specific events but has to keep its memory limited to only 25. As a result there is no need for dynamic memory allocation. Every node holds its data plus the prev and next pointers. The EventLog therefore uses a Last In First Out operation to append at the tail and remove from the head. A doubly linked list does all three in O(1) without shifting elements and the prev link is what makes stepping backwards constant time. The list was a specified structure and it excercises pointer handling and manual memory management at the cost of one allocation per event.

### Design decisions
1. Event memory is allocated first before eviction happens. If malloc fails, the existing log is untouched instead of having lost its oldest event. 
2. The pointer to the freed node moves to the new head upon eviction.
3. ID and Severity are validated separately. IDs are uniquely identified so that every evnt session has its unique ID that does not reset. Also, the severity is validated to confirm that no given severity is beyond the specified range.

Compile and run
``` gcc -Wall -Wextra -std=c99 -o log_events log_events.c```
```./log_events```



## Q2: Network Device Command Registry

Technicians enter configuration commands for routers, gateways and controllers. The program then loads the approved commands from commands.txt approves an exact match, suggests the likely intended command for near misses and records every unapproved attempt with a timestamp. A binary search tree of at most 50 dynamically allocated nodes gives fast verification of approved commands and the program must not crash on an empty tree or invalid input.

### Design and Implementation

1. The BST lookup follows one branch and an in-order traversal produces the alphabetical listing with no sorting step
2. Each node holds two child pointers and the command text in a flexible array member so that there is only one malloc and one free per command
3. In a network sesitive configuration, a differently cased string is assumed to be a different string hence (show_status) receives a hint but is not approved.

Compile and run
```gcc -Wall -Wextra -std=c99 -o command registry command_registry.c```
```./command_registry```


## Q3: Server Dependency Network Analyzer

An admin model services as vertices and a dependency such as web servers to API server as a directed edge. The program must report direct dependencies and direct dependent, add and remove servers and dependencies. Furethermore it should also display the adjacency matrix and survive missing servers, duplicates and self references and empty a graph.

### Design and Implemetation

1. Adjacency lists as the primary structure. Space is O (V+E) and listing a server's dependencies walks only on its own edges.
2. Servers and edges are appended so aoutput matches the specifications example order.
3. A server cannot depend on itself, instead they can have two way communication ( A TO B and B TO A)

Compile and Run
```gcc -Wall -Wextra -std=c99 -o network_analyzer server-network_analyzer.c```
```./network_analyzer```


## Q4: Emergency Medical Supply Routing

A weighted graph of locations and roads , searched with Djikistra's algorithm and a predecessor array, gives the dispatcher the shortest route and its total distance between the two locations. The dispatcher chooses a start and a destination and the program must find the minimum sdistance route.

### Structure and Design

1. Djikistra's algorithm repeatedly settles the unvisited location with the smallest distance and relaxes its roads. 
2. Whenever a shorter way is found, its predecessor is recorded. The route is rebuilt by walking predecessors back from the destination and filling an array from the end so the output is the actual route and not just the distance.
3. Distances must be whole kilometres from 1 to 100,000, this also keeps sums far from overflow since distances are accumulated in 64 bit integers.

Compile and run

```gcc -Wall -Wextra -std=c99 -o medical_routing medical_routing.c```
```./medical_routing```

## Q5: Embedded Firmware Log Compression Utility

Remote monitoring devices have limited badnwidth and so their reading have to be compressed before transmission. The program counts character frequencies and builds a huffman tree. It can also decompress and automatically compare the result with the original and run from a menu that survives missing files and invalide choices. The compression must be lossless.

### Design and Implementation

1. Each node holds a frequency, creation order, symbol and 2 child pointers.
2. Binary mean heap over at most 256 node pointers. It compares frequency first and creation order second. Ewual frequencies therefore always resolve the same way and the output is reproducible.
3. Original size stored so padding bits are never decoded and truncation can be detected. A file with one distinct byte gets the one bit code 0.
4. The compressed file is treated as untrusted input. Tree depth is capped at 256 and the node count at 511, duplicate symbols are rejected. The size field is also checked against bits that are present before any allocation and trailing bytes are rejected.
5. Every heap object has one owner and one release point. Flushing the final byte tetst the failure flag because without that test an out of memory condition would loop forever.

Compile and Run

```gcc -Wall -Wextra -std=c99 -o log-compression_1 log-compression.c```
```./log-compression_1```