# include <ctype.h>
# include <errno.h>
# include <limits.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>

# define MAX_NAME 50
# define MAX_LOCATIONS 1000
# define MAX_ROAD_KM 100000
# define INPUT_BUF 256
# define INF LLONG_MAX

# ifndef ARROW
# define ARROW " \xe2\x86\x92 "
# endif

typedef struct Road {
    int to;
    int km;
    struct Road *next;
} Road;

typedef struct {
    char  name[MAX_NAME];
    Road *roads;
} Location;

typedef struct {
    Location *loc;
    int count;
    int cap;
} Graph;

typedef struct {
    int *stops;
    int len;
    long long total;
} Route;

typedef enum {
    ROAD_OK, ROAD_SELF, ROAD_DUPLICATE, ROAD_BAD_DISTANCE,
    ROAD_TOO_MANY_LOCATIONS, ROAD_NO_MEMORY
} RoadResult;

typedef enum { READ_OK, READ_EOF, READ_TOO_LONG } ReadStatus;

# define LOC_INVALID (-1)
# define LOC_EOF     (-2)

static const char *const DEFAULT_ROADS[] = {
    "Central Warehouse --8-- Distribution Center",
    "Central Warehouse --14-- District Hospital",
    "Distribution Center --5-- Health Center A",
    "Distribution Center --11-- Health Center B",
    "Health Center A --7-- District Hospital",
    "Health Center B --3-- Health Center C",
    "Health Center C --6-- District Hospital",
    "Health Center B --9-- District Hospital",
    "Health Center A --4-- Health Center C"
};

static int names_equal(const char *a, const char *b){
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static char *trim(char *s){
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}
static const char *name_error(const char *s){
    size_t n = strlen(s);
    if (n == 0)
        return "No name entered";
    if (n > MAX_NAME - 1)
        return "location name is too long";
    for (; *s != '\0'; s++)
        if (!isprint((unsigned char)*s))
            return "Name contains invalid characters";
    return NULL;
}

static ReadStatus read_line(FILE *in, const char *prompt, char *buf, size_t size){
    if (prompt != NULL) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (fgets(buf, (int)size, in) == NULL) {
        buf[0] = '\0';
        return READ_EOF;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        return READ_OK;
    }
    if (feof(in))
        return READ_OK;

    int ch;
    while ((ch = fgetc(in)) != '\n' && ch != EOF)
        ;
    return READ_TOO_LONG;
}

static void graph_init(Graph *g){
    g -> loc = NULL;
    g -> count = 0;
    g -> cap = 0;
}

static void graph_destroy(Graph *g){
    for (int i = 0; i < g->count; i++) {
        Road *r = g->loc[i].roads;
        while (r != NULL) {
            Road *next = r -> next;
            free(r);
            r = next;
        }
    }
    free(g -> loc);
    graph_init(g);
}

static int graph_find(const Graph *g, const char *name){
    for (int i = 0; i < g->count; i++)
        if (names_equal(g->loc[i].name, name))
            return i;
    return -1;
}

static int graph_add_location(Graph *g, const char *name){
    if (g->count >= MAX_LOCATIONS)
        return -1;
    if (g->count == g->cap) {
        int ncap = g->cap ? g->cap * 2 : 8;
        Location *p = realloc(g->loc, (size_t)ncap * sizeof *p);
        if (p == NULL)
            return -1;
        g->loc = p;
        g->cap = ncap;
    }
    Location *l = &g->loc[g->count];
    snprintf(l->name, sizeof l->name, "%s", name);
    l->roads = NULL;
    return g->count++;
}


static int graph_road_km(const Graph *g, int a, int b){
    for (const Road *r = g->loc[a].roads; r != NULL; r = r->next)
        if (r->to == b)
            return r->km;
    return -1;
}

static void append_road(Road **head, Road *r){
    while (*head != NULL)
        head = &(*head)->next;
    *head = r;
}

static RoadResult graph_add_road(Graph *g, const char *name_a, const char *name_b, long km){
    if (km < 1 || km > MAX_ROAD_KM)
        return ROAD_BAD_DISTANCE;
    if (names_equal(name_a, name_b))
        return ROAD_SELF;

    int a = graph_find(g, name_a);
    int b = graph_find(g, name_b);
    if (a >= 0 && b >= 0 && graph_road_km(g, a, b) >= 0)
        return ROAD_DUPLICATE;

    int new_locations = (a < 0) + (b < 0);
    if (g->count + new_locations > MAX_LOCATIONS)
        return ROAD_TOO_MANY_LOCATIONS;

    Road *ra = malloc(sizeof *ra);
    Road *rb = malloc(sizeof *rb);
    if (ra == NULL || rb == NULL) {
        free(ra);
        free(rb);
        return ROAD_NO_MEMORY;
    }

    if (a < 0)
        a = graph_add_location(g, name_a);
    if (b < 0)
        b = graph_add_location(g, name_b);
    if (a < 0 || b < 0) {
        free(ra);
        free(rb);
        return ROAD_NO_MEMORY;
    }

    ra->to = b; ra->km = (int)km; ra->next = NULL;
    rb->to = a; rb->km = (int)km; rb->next = NULL;
    append_road(&g->loc[a].roads, ra);
    append_road(&g->loc[b].roads, rb);
    return ROAD_OK;
}
static int load_road_line(Graph *g, const char *line, char *err, size_t errsz){
    char buf[INPUT_BUF];
    snprintf(buf, sizeof buf, "%s", line);

    char *p1 = strstr(buf, "--");
    char *p2 = (p1 != NULL) ? strstr(p1 + 2, "--") : NULL;
    if (p1 == NULL || p2 == NULL) {
        snprintf(err, errsz, "expected format 'Location A --km-- Location B'");
        return 0;
    }
    *p1 = '\0';
    *p2 = '\0';
    char *a = trim(buf);
    char *w = trim(p1 + 2);
    char *b = trim(p2 + 2);

    const char *nerr = name_error(a);
    if (nerr == NULL)
        nerr = name_error(b);
    if (nerr != NULL) {
        snprintf(err, errsz, "%s", nerr);
        return 0;
    }

    if (*w == '\0') {
        snprintf(err, errsz, "missing distance");
        return 0;
    }
    char *end;
    errno = 0;
    long km = strtol(w, &end, 10);
    if (end == w || *end != '\0') {
        snprintf(err, errsz, "distance '%s' is not a whole number", w);
        return 0;
    }
    if (errno == ERANGE)
        km = LONG_MAX;

    switch (graph_add_road(g, a, b, km)) {
    case ROAD_OK:
        return 1;
    case ROAD_BAD_DISTANCE:
        if (km == LONG_MAX)
            snprintf(err, errsz, "distance out of range (must be 1-%d km)", MAX_ROAD_KM);
        else
            snprintf(err, errsz, "invalid distance %ld (must be 1-%d km)", km, MAX_ROAD_KM);
        return 0;
    case ROAD_SELF:
        snprintf(err, errsz, "a road cannot connect a location to itself");
        return 0;
    case ROAD_DUPLICATE:
        snprintf(err, errsz, "road between '%s' and '%s' already defined", a, b);
        return 0;
    case ROAD_TOO_MANY_LOCATIONS:
        snprintf(err, errsz, "too many locations (max %d)", MAX_LOCATIONS);
        return 0;
    case ROAD_NO_MEMORY:
    default:
        snprintf(err, errsz, "out of memory");
        return 0;
    }
}

static void load_default_network(Graph *g){
    char err[128];
    for (size_t i = 0; i < sizeof DEFAULT_ROADS / sizeof DEFAULT_ROADS[0]; i++)
        if (!load_road_line(g, DEFAULT_ROADS[i], err, sizeof err))
            printf("Internal error loading built-in road %zu: %s\n", i + 1, err);
}
static int load_network_file(Graph *g, const char *path){
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        printf("Error: cannot open '%s': %s\n", path, strerror(errno));
        return 0;
    }

    char line[INPUT_BUF], err[128];
    int line_no = 0, loaded = 0, skipped = 0;
    ReadStatus rs;

    while ((rs = read_line(f, NULL, line, sizeof line)) != READ_EOF) {
        line_no++;
        if (rs == READ_TOO_LONG) {
            printf("  line %d skipped: line too long\n", line_no);
            skipped++;
            continue;
        }
        char *s = trim(line);
        if (*s == '\0' || *s == '#')
            continue;
        if (load_road_line(g, s, err, sizeof err)) {
            loaded++;
        } else {
            printf("  line %d skipped: %s\n", line_no, err);
            skipped++;
        }
    }
    fclose(f);

    printf("Loaded %d road(s) connecting %d location(s) from '%s'.\n",
           loaded, g->count, path);
    if (skipped)
        printf("  %d line(s) skipped.\n", skipped);
    if (g->count == 0)
        puts("Warning: no valid roads were loaded.");
    return 1;
}

static int dijkstra(const Graph *g, int src, int dst, long long *dist, int *prev){
    int n = g->count;
    char *done = calloc((size_t)n, 1);
    if (done == NULL)
        return 0;

    for (int i = 0; i < n; i++) {
        dist[i] = INF;
        prev[i] = -1;
    }
    dist[src] = 0;

    for (int iter = 0; iter < n; iter++) {
        int u = -1;
        for (int i = 0; i < n; i++)
            if (!done[i] && dist[i] != INF && (u == -1 || dist[i] < dist[u]))
                u = i;
        if (u == -1)
            break;
        done[u] = 1;
        if (u == dst)
            break;

        for (const Road *r = g->loc[u].roads; r != NULL; r = r->next) {
            if (!done[r->to] && dist[u] + r->km < dist[r->to]) {
                dist[r->to] = dist[u] + r->km;
                prev[r->to] = u;
            }
        }
    }
    free(done);
    return 1;
}
static int build_path(const int *prev, int dst, int **out){
    int len = 0;
    for (int v = dst; v != -1; v = prev[v])
        len++;

    int *stops = malloc((size_t)len * sizeof *stops);
    if (stops == NULL)
        return 0;

    int i = len;
    for (int v = dst; v != -1; v = prev[v])
        stops[--i] = v;
    *out = stops;
    return len;
}

static void route_clear(Route *r){
    free(r->stops);
    r->stops = NULL;
    r->len = 0;
    r->total = 0;
}

static void print_route_names(const Graph *g, const int *stops, int len){
    for (int i = 0; i < len; i++)
        printf("%s%s", i ? ARROW : "", g->loc[stops[i]].name);
    putchar('\n');
}

static int prompt_location(const Graph *g, const char *prompt){
    char buf[INPUT_BUF];
    ReadStatus rs = read_line(stdin, prompt, buf, sizeof buf);
    if (rs == READ_EOF) {
        putchar('\n');
        return LOC_EOF;
    }
    if (rs == READ_TOO_LONG) {
        puts("Invalid input: entry too long.");
        return LOC_INVALID;
    }

    char *s = trim(buf);
    if (*s == '\0') {
        puts("Invalid input: no location entered.");
        return LOC_INVALID;
    }

    int idx = graph_find(g, s);
    if (idx >= 0)
        return idx;

    char *end;
    errno = 0;
    long num = strtol(s, &end, 10);
    if (end != s && *end == '\0' && errno == 0 && num >= 1 && num <= g->count)
        return (int)(num - 1);

    printf("Location '%s' not found. Use option 1 to see valid locations.\n", s);
    return LOC_INVALID;
}

static void cmd_display_locations(const Graph *g){
    if (g->count == 0) {
        puts("No locations are loaded.");
        return;
    }
    puts("Locations (with their direct roads):");
    for (int i = 0; i < g->count; i++) {
        printf("  %d. %s\n", i + 1, g->loc[i].name);
        for (const Road *r = g->loc[i].roads; r != NULL; r = r->next)
            printf("       - %s (%d km)\n", g->loc[r->to].name, r->km);
    }
}

static void cmd_find_route(const Graph *g, Route *last){
    if (g->count == 0) {
        puts("No locations are loaded.");
        return;
    }

    int src = prompt_location(g, "Start location (name or number): ");
    if (src < 0)
        return;
    int dst = prompt_location(g, "Destination (name or number): ");
    if (dst < 0)
        return;

    if (src == dst) {
        printf("Start and destination are both '%s'. Choose two different locations.\n",
               g->loc[src].name);
        return;
    }

    long long *dist = malloc((size_t)g->count * sizeof *dist);
    int *prev = malloc((size_t)g->count * sizeof *prev);
    if (dist == NULL || prev == NULL || !dijkstra(g, src, dst, dist, prev)) {
        puts("Error: out of memory while calculating the route.");
        free(dist);
        free(prev);
        return;
    }

    if (dist[dst] == INF) {
        printf("No route exists from %s to %s.\n", g->loc[src].name, g->loc[dst].name);
    } else {
        int *stops = NULL;
        int len = build_path(prev, dst, &stops);
        if (len == 0) {
            puts("Error: out of memory while building the route.");
        } else {
            route_clear(last);
            last->stops = stops;
            last->len = len;
            last->total = dist[dst];

            puts("\nShortest Route:");
            print_route_names(g, stops, len);
            printf("Total Distance: %lld km\n", dist[dst]);
        }
    }
    free(dist);
    free(prev);
}

static void cmd_route_distance(const Graph *g, const Route *last)
{
    if (last->len == 0) {
        puts("No route has been calculated yet. Use option 2 first.");
        return;
    }

    printf("\nLast route: %s%s%s\n", g->loc[last->stops[0]].name, ARROW,
           g->loc[last->stops[last->len - 1]].name);
    for (int i = 0; i + 1 < last->len; i++) {
        int a = last->stops[i], b = last->stops[i + 1];
        printf("  %s%s%s: %d km\n", g->loc[a].name, ARROW, g->loc[b].name,
               graph_road_km(g, a, b));
    }
    printf("Total Distance: %lld km\n", last->total);
}

static void print_menu(void){
    puts("\n Medical Supply Delivery Router");
    puts("1. Display locations");
    puts("2. Find shortest route");
    puts("3. Display route distance");
    puts("4. Exit");
}

int main(int argc, char **argv){
    Graph g;
    Route last = { NULL, 0, 0 };
    graph_init(&g);

    if (argc > 1) {
        if (!load_network_file(&g, argv[1])) {
            graph_destroy(&g);
            return 1;
        }
    } else {
        load_default_network(&g);
        printf("Loaded built-in network: %d locations.\n", g.count);
    }

    for (;;) {
        print_menu();
        char line[16];
        ReadStatus rs = read_line(stdin, "Enter choice: ", line, sizeof line);
        if (rs == READ_EOF) {
            putchar('\n');
            break;
        }

        char *opt = trim(line);
        if (rs == READ_TOO_LONG || opt[0] == '\0' || opt[1] != '\0') {
            puts("Invalid choice.");
            continue;
        }

        int done = 0;
        switch (opt[0]) {
        case '1': cmd_display_locations(&g);      break;
        case '2': cmd_find_route(&g, &last);      break;
        case '3': cmd_route_distance(&g, &last);  break;
        case '4': done = 1;                       break;
        default:  puts("Invalid choice.");
        }
        if (done)
            break;
    }

    route_clear(&last);
    graph_destroy(&g);
    puts("Goodbye.");
    return 0;
}