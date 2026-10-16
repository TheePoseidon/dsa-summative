#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME 50
#define INPUT_BUF 128

typedef struct Edge {
    struct Vertex *to;
    struct Edge   *next;
} Edge;

typedef struct Vertex {
    char name[MAX_NAME];
    Edge *out;
    struct Vertex *next;
} Vertex;

typedef struct {
    Vertex *head;
    int count;
} Graph;

typedef enum { ADD_OK, ADD_DUPLICATE, ADD_NO_MEMORY } AddResult;
typedef enum { EDGE_OK, EDGE_DUPLICATE, EDGE_SELF, EDGE_NO_MEMORY } EdgeResult;
typedef enum { READ_OK, READ_EOF, READ_TOO_LONG } ReadStatus;
typedef enum { NAME_OK, NAME_INVALID, NAME_EOF } NameStatus;

static void graph_init(Graph *g)
{
    g->head = NULL;
    g->count = 0;
}
static int names_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static Vertex *find_vertex(const Graph *g, const char *name)
{
    for (Vertex *v = g->head; v != NULL; v = v->next)
        if (names_equal(v->name, name))
            return v;
    return NULL;
}

static int has_edge(const Vertex *from, const Vertex *to)
{
    for (const Edge *e = from->out; e != NULL; e = e->next)
        if (e->to == to)
            return 1;
    return 0;
}
static AddResult graph_add_vertex(Graph *g, const char *name)
{
    if (find_vertex(g, name) != NULL)
        return ADD_DUPLICATE;

    Vertex *v = malloc(sizeof *v);
    if (v == NULL)
        return ADD_NO_MEMORY;
    snprintf(v->name, sizeof v->name, "%s", name);
    v->out = NULL;
    v->next = NULL;

    Vertex **link = &g->head;
    while (*link != NULL)
        link = &(*link)->next;
    *link = v;
    g->count++;
    return ADD_OK;
}
static EdgeResult graph_add_edge(Vertex *from, Vertex *to)
{
    if (from == to)
        return EDGE_SELF;
    if (has_edge(from, to))
        return EDGE_DUPLICATE;

    Edge *e = malloc(sizeof *e);
    if (e == NULL)
        return EDGE_NO_MEMORY;
    e->to = to;
    e->next = NULL;

    Edge **link = &from->out;
    while (*link != NULL)
        link = &(*link)->next;
    *link = e;
    return EDGE_OK;
}
static int graph_remove_edge(Vertex *from, const Vertex *to)
{
    for (Edge **link = &from->out; *link != NULL; link = &(*link)->next) {
        if ((*link)->to == to) {
            Edge *dead = *link;
            *link = dead->next;
            free(dead);
            return 1;
        }
    }
    return 0;
}

static int free_edges(Edge *e)
{
    int n = 0;
    while (e != NULL) {
        Edge *next = e->next;
        free(e);
        e = next;
        n++;
    }
    return n;
}

static int graph_remove_vertex(Graph *g, const char *name)
{
    Vertex **link = &g->head;
    while (*link != NULL && !names_equal((*link)->name, name))
        link = &(*link)->next;
    if (*link == NULL)
        return -1;

    Vertex *victim = *link;
    *link = victim->next;
    g->count--;

    int removed = 0;
    for (Vertex *v = g->head; v != NULL; v = v->next)
        removed += graph_remove_edge(v, victim);
    removed += free_edges(victim->out);

    free(victim);
    return removed;
}

static void graph_destroy(Graph *g)
{
    Vertex *v = g->head;
    while (v != NULL) {
        Vertex *next = v->next;
        free_edges(v->out);
        free(v);
        v = next;
    }
    g->head = NULL;
    g->count = 0;
}

static int index_of(const Graph *g, const Vertex *target)
{
    int i = 0;
    for (const Vertex *v = g->head; v != NULL; v = v->next, i++)
        if (v == target)
            return i;
    return -1;
}

static unsigned char *build_matrix(const Graph *g)
{
    int n = g->count;
    unsigned char *m = calloc((size_t)n * (size_t)n, 1);
    if (m == NULL)
        return NULL;

    int i = 0;
    for (const Vertex *v = g->head; v != NULL; v = v->next, i++)
        for (const Edge *e = v->out; e != NULL; e = e->next)
            m[(size_t)i * (size_t)n + (size_t)index_of(g, e->to)] = 1;
    return m;
}

static void print_matrix(const Graph *g)
{
    int n = g->count;
    unsigned char *m = build_matrix(g);
    if (m == NULL) {
        puts("Error: out of memory while building the matrix.");
        return;
    }

    int w = 0;
    for (const Vertex *v = g->head; v != NULL; v = v->next) {
        int len = (int)strlen(v->name);
        if (len > w)
            w = len;
    }

    puts("Adjacency matrix (row depends on column; 1 = direct dependency):\n");

    printf("%*s", w + 6, "");
    for (int j = 0; j < n; j++)
        printf("%3d", j);
    putchar('\n');

    printf("%*s", w + 6, "");
    for (int j = 0; j < 3 * n; j++)
        putchar('-');
    putchar('\n');

    int i = 0;
    for (const Vertex *v = g->head; v != NULL; v = v->next, i++) {
        printf("%3d %-*s |", i, w, v->name);
        for (int j = 0; j < n; j++)
            printf("%3d", m[(size_t)i * (size_t)n + (size_t)j]);
        putchar('\n');
    }
    free(m);
}

static ReadStatus read_line(const char *prompt, char *buf, size_t size)
{
    fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(buf, (int)size, stdin) == NULL) {
        buf[0] = '\0';
        return READ_EOF;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
        return READ_OK;
    }
    if (feof(stdin))
        return READ_OK;

    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF)
        ;
    return READ_TOO_LONG;
}

static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

static const char *name_error(const char *s)
{
    size_t n = strlen(s);
    if (n == 0)
        return "name cannot be empty";
    if (n > MAX_NAME - 1)
        return "name too long (max 49 characters)";
    for (; *s != '\0'; s++)
        if (!isprint((unsigned char)*s))
            return "name contains non-printable characters";
    return NULL;
}
static NameStatus prompt_name(const char *prompt, char *out)
{
    char buf[INPUT_BUF];
    ReadStatus rs = read_line(prompt, buf, sizeof buf);

    if (rs == READ_EOF) {
        putchar('\n');
        return NAME_EOF;
    }
    if (rs == READ_TOO_LONG) {
        puts("Invalid input: name too long (max 49 characters).");
        return NAME_INVALID;
    }

    char *name = trim(buf);
    const char *err = name_error(name);
    if (err != NULL) {
        printf("Invalid input: %s.\n", err);
        return NAME_INVALID;
    }
    snprintf(out, MAX_NAME, "%s", name);
    return NAME_OK;
}

static int report_if_empty(const Graph *g)
{
    if (g->count == 0) {
        puts("The graph is empty. Use 'a' to add a server.");
        return 1;
    }
    return 0;
}

static void cmd_add_server(Graph *g)
{
    char name[MAX_NAME];
    if (prompt_name("Server name: ", name) != NAME_OK)
        return;

    switch (graph_add_vertex(g, name)) {
    case ADD_OK:
        printf("Server '%s' added (%d total).\n", name, g->count);
        break;
    case ADD_DUPLICATE:
        printf("Server '%s' already exists. Nothing added.\n", name);
        break;
    case ADD_NO_MEMORY:
        puts("Error: out of memory. Server not added.");
        break;
    }
}

static void cmd_remove_server(Graph *g)
{
    if (report_if_empty(g))
        return;

    char name[MAX_NAME];
    if (prompt_name("Server to remove: ", name) != NAME_OK)
        return;

    Vertex *v = find_vertex(g, name);
    if (v == NULL) {
        printf("Server '%s' not found.\n", name);
        return;
    }

    char canonical[MAX_NAME];
    snprintf(canonical, sizeof canonical, "%s", v->name);

    int removed = graph_remove_vertex(g, canonical);
    printf("Server '%s' removed along with %d related dependenc%s.\n",
           canonical, removed, removed == 1 ? "y" : "ies");
}
static int read_dependency(const Graph *g, Vertex **from, Vertex **to)
{
    char a[MAX_NAME], b[MAX_NAME];

    if (prompt_name("Dependent server (the one that needs another): ", a) != NAME_OK)
        return 0;
    if (prompt_name("Depends on (the server it needs): ", b) != NAME_OK)
        return 0;

    *from = find_vertex(g, a);
    *to = find_vertex(g, b);
    if (*from == NULL)
        printf("Server '%s' not found.\n", a);
    if (*to == NULL)
        printf("Server '%s' not found.\n", b);
    return *from != NULL && *to != NULL;
}

static void cmd_create_dependency(Graph *g)
{
    if (report_if_empty(g))
        return;

    Vertex *from, *to;
    if (!read_dependency(g, &from, &to))
        return;

    switch (graph_add_edge(from, to)) {
    case EDGE_OK:
        printf("Dependency created: %s -> %s.\n", from->name, to->name);
        break;
    case EDGE_DUPLICATE:
        printf("Dependency %s -> %s already exists. Nothing added.\n",
               from->name, to->name);
        break;
    case EDGE_SELF:
        puts("A server cannot depend on itself.");
        break;
    case EDGE_NO_MEMORY:
        puts("Error: out of memory. Dependency not created.");
        break;
    }
}

static void cmd_delete_dependency(Graph *g)
{
    if (report_if_empty(g))
        return;

    Vertex *from, *to;
    if (!read_dependency(g, &from, &to))
        return;

    if (graph_remove_edge(from, to))
        printf("Dependency removed: %s -> %s.\n", from->name, to->name);
    else
        printf("No dependency %s -> %s exists.\n", from->name, to->name);
}

static void cmd_query(const Graph *g)
{
    if (report_if_empty(g))
        return;

    char name[MAX_NAME];
    if (prompt_name("Server: ", name) != NAME_OK)
        return;

    const Vertex *v = find_vertex(g, name);
    if (v == NULL) {
        printf("Server '%s' not found.\n", name);
        return;
    }

    printf("\nServer: %s\n", v->name);

    puts("Direct dependencies:");
    int n = 0;
    for (const Edge *e = v->out; e != NULL; e = e->next, n++)
        printf("  %s\n", e->to->name);
    if (n == 0)
        puts("  (none)");

    printf("Services depending on %s:\n", v->name);
    n = 0;
    for (const Vertex *u = g->head; u != NULL; u = u->next) {
        if (has_edge(u, v)) {
            printf("  %s\n", u->name);
            n++;
        }
    }
    if (n == 0)
        puts("  (none)");
}

static void cmd_matrix(const Graph *g)
{
    if (report_if_empty(g))
        return;
    print_matrix(g);
}

static void print_help(void)
{
    puts("Commands: a=add server  r=remove server  c=create dependency  "
         "d=delete dependency  q=query server  m=matrix  x=exit");
}

static const char *const INITIAL_SERVERS[] = {
    "Web Server", "API Server", "Authentication Server",
    "Database Server", "Cache Server", "Backup Server"
};

static const char *const INITIAL_DEPENDENCIES[][2] = {
    { "Web Server", "API Server" },
    { "Web Server", "Authentication Server" },
    { "API Server", "Database Server" },
    { "API Server", "Cache Server" },
    { "Authentication Server", "Database Server" },
    { "Cache Server", "Database Server" },
    { "Database Server", "Backup Server" }
};

static int build_initial_graph(Graph *g)
{
    for (size_t i = 0; i < sizeof INITIAL_SERVERS / sizeof INITIAL_SERVERS[0]; i++)
        if (graph_add_vertex(g, INITIAL_SERVERS[i]) != ADD_OK)
            return 0;

    for (size_t i = 0; i < sizeof INITIAL_DEPENDENCIES / sizeof INITIAL_DEPENDENCIES[0]; i++) {
        Vertex *from = find_vertex(g, INITIAL_DEPENDENCIES[i][0]);
        Vertex *to = find_vertex(g, INITIAL_DEPENDENCIES[i][1]);
        if (from == NULL || to == NULL || graph_add_edge(from, to) != EDGE_OK)
            return 0;
    }
    return 1;
}

int main(void)
{
    Graph g;
    graph_init(&g);

    if (!build_initial_graph(&g)) {
        puts("Error: could not build the initial graph (out of memory).");
        graph_destroy(&g);
        return 1;
    }

    printf("Loaded %d servers.\n", g.count);
    print_help();

    for (;;) {
        char line[16];
        ReadStatus rs = read_line("> ", line, sizeof line);
        if (rs == READ_EOF) {
            putchar('\n');
            break;
        }

        char *opt = trim(line);
        if (rs == READ_OK && *opt == '\0')
            continue;
        if (rs == READ_TOO_LONG || opt[1] != '\0') {
            puts("Unknown option.");
            print_help();
            continue;
        }

        int done = 0;
        switch (tolower((unsigned char)opt[0])) {
        case 'a': cmd_add_server(&g);        break;
        case 'r': cmd_remove_server(&g);     break;
        case 'c': cmd_create_dependency(&g); break;
        case 'd': cmd_delete_dependency(&g); break;
        case 'q': cmd_query(&g);             break;
        case 'm': cmd_matrix(&g);            break;
        case 'x': done = 1;                  break;
        default:
            puts("Unknown option.");
            print_help();
        }
        if (done)
            break;
    }

    graph_destroy(&g);
    puts("Goodbye.");
    return 0;
}