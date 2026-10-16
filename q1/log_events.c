# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <time.h>
# include <unistd.h>

# define MAX_EVENTS 25
# define FIELD_LEN 32
# define SAVE_FILE "log_events.txt"

typedef struct Event {
    int id;
    char type[FIELD_LEN];
    time_t timestamp;
    char sensor[FIELD_LEN];
    int severity;
    struct Event *prev;
    struct Event *next;
} Event;

typedef struct {
    Event *head;
    Event *tail;
    Event *current;
    int count;
    int next_id;
} EventLog;

static void log_init(EventLog *log){
    log -> head = log -> tail = log -> current = NULL;
    log -> count = 0;
    log -> next_id = 1;
}

static void log_remove_oldest(EventLog *log){
    Event *old = log -> head;
    if (old == NULL)
        return;
    log -> head = old -> next;
    if (log -> head != NULL)
        log -> head -> prev = NULL;
    else
        log -> tail = NULL;
    if (log -> current == old)
        log -> current = log -> head;
    free(old);
    log -> count--;
}

static Event *log_add(EventLog *log, const char *type, const char *sensor, int severity){
    Event *e = malloc(sizeof *e);
    if (e == NULL)
        return NULL;
    e -> id = log -> next_id++;
    snprintf(e -> type, sizeof e-> type, "%s", type);
    snprintf(e -> sensor, sizeof e-> sensor, "%s", sensor);
    e -> timestamp = time(NULL);
    e -> severity = severity;
    e -> next = NULL;
    if (log -> count >= MAX_EVENTS)
        log_remove_oldest(log);
    e -> prev = log -> tail;
    if (log -> tail != NULL)
        log -> tail -> next = e;
    else
        log -> head = e;
    log -> tail = e;
    log -> count++;
    if (log -> current == NULL)
        log -> current = e;
    return e;
}

static void log_clear(EventLog *log){
    Event *e = log -> head;
    while (e != NULL) {
        Event *next = e -> next;
        free(e);
        e = next;
    }
    log -> head = log -> tail = log -> current = NULL;
    log -> count = 0;
}

static void format_event(const Event *e, FILE *out){
    char tbuf[32];
    struct tm tmv;
    localtime_r(&e -> timestamp, &tmv);
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(out, "ID: %d | Type: %s | Sensor: %s | Time: %s | Severity: %d\n",
            e -> id, e -> type, e -> sensor, tbuf, e -> severity);
}

static void show_current(const EventLog *log){
    if(log -> current == NULL){
        puts("Nothing to show here.");
        return;
    }
    printf("Current Events: ");
    format_event(log -> current, stdout);
}

static void go_next(EventLog *log){
    if (log -> current == NULL){
        puts("This is the newest event.");
        show_current(log);
    } else {
        log -> current = log -> current -> next;
        show_current(log);
    }
}

static void go_prev(EventLog *log){
    if (log -> current == NULL)
        puts("Event log is empty.");
    else if (log -> current -> prev == NULL){
        puts("This is the oldest event.");
        show_current(log);
    } else {
        log -> current = log -> current -> prev;
        show_current(log);
    }
}
static int save_session(const EventLog *log){
    FILE *f = fopen(SAVE_FILE, "w");
    if (f == NULL)
        return -1;
    fprintf(f, "# Event Log Session\n", log -> count);
    for (const Event *e  = log -> head; e != NULL; e = e -> next)
        format_event(e, f);
    fclose(f);
    return 0;
}

static int read_line(const char *prompt, char *buf, size_t size){
    printf("%s", prompt);
    if (fgets(buf, (int)size, stdin) == NULL)
        return 0;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF);
    }
    return 1;
}

static void add_event_interactive(EventLog *log){
    char type[FIELD_LEN], sensor[FIELD_LEN], sev[16];
    if (!read_line("Enter event type: ", type, sizeof type) || type[0] == '\0') {
        puts("Event type cannot be empty.");
        return;
    }
    if (!read_line("Sensor ID: ", sensor, sizeof sensor) || sensor[0] == '\0') {
        puts("Sensor ID required.");
        return;
    }
    if (!read_line("Severity: ", sev, sizeof sev)) {
        puts("Event not added.");
        return;
    }
    char *end;
    long s = strtol(sev, &end, 10);
    if (end == sev || *end != '\0' || s < 1 || s > 5){
        puts("Severity must be a number between 1 and 5.");
        return;
    }
    int was_full = (log -> count >= MAX_EVENTS);
    Event *e = log_add(log, type, sensor, (int)s);
    if (e == NULL) {
        puts("Failed to add event.");
        return;
    }
    if (was_full)
        puts("Event log was full. Oldest event removed.");
    printf("Event added: %d (Total: %d/%d)\n", e -> id, log -> count, MAX_EVENTS);
    return;
}

static void print_help(void){
    puts("Commands: n - next, p - previous, a - add event, c - current, x - clear, q - save and quit");
}
