DSA in C summative

# Project Report
## Q1: Embedded Device Event Logger

A monitoring device records specific events but has to keep its memory limited to only 25. As a result there is no need for dynamic memory allocation. Every node holds its data plus the prev and next pointers. The EventLog therefore uses a Last In First Out operation to append at the tail and remove from the head. A doubly linked list does all three in O(1) without shifting elements and the prev link is what makes stepping backwards constant time. The list was a specified structure and it excercises pointer handling and manual memory management at the cost of one allocation per event.

### Design decisions
1. Event memory is allocated first before eviction happens. If malloc fails, the existing log is untouched instead of having lost its oldest event. 
2. The pointer to the freed node moves to the new head upon eviction.
3. ID and Severity are validated separately. IDs are uniquely identified so that every evnt session has its unique ID that does not reset. Also, the severity is validated to confirm that no given severity is beyond the specified range.

Build and run
``` gcc -Wall -Wextra -std=c99 -o log_events log_events.c```
```./log_events```

## Q2: Network Device Command Registry
