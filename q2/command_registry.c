#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_COMMANDS     50
#define MAX_CMD_LEN      63
#define INPUT_BUF        128
#define DEFAULT_CMD_FILE "commands.txt"
#define AUDIT_FILE       "audit.log"
#define SESSION_FILE     "session_summary.txt"

typedef struct Node {
    struct Node *left;
    struct Node *right;
    char cmd[];
} Node;

typedef struct {
    Node *root;
    int count;
} Registry;

typedef struct {
    int approved;
    int suggested;
    int unknown;
    int invalid;
} Session;

typedef enum { INSERT_OK, INSERT_DUPLICATE, INSERT_FULL, INSERT_NO_MEMORY } InsertResult;
typedef enum { READ_OK, READ_EOF, READ_TOO_LONG } ReadStatus;


static void registry_init(Registry *reg)
{
    reg->root = NULL;
    reg->count = 0;
}

static InsertResult bst_insert(Registry *reg, const char *cmd)
{
    Node **link = &reg->root;
    while (*link != NULL) {
        int c = strcmp(cmd, (*link)->cmd);
        if (c == 0)
            return INSERT_DUPLICATE;
        link = (c < 0) ? &(*link)->left : &(*link)->right;
    }

    if (reg->count >= MAX_COMMANDS)
        return INSERT_FULL;

    Node *n = malloc(sizeof *n + strlen(cmd) + 1);
    if (n == NULL)
        return INSERT_NO_MEMORY;
    n->left = n->right = NULL;
    strcpy(n->cmd, cmd);

    *link = n;
    reg->count++;
    return INSERT_OK;
}

static const Node *bst_find(const Node *root, const char *cmd)
{
    while (root != NULL) {
        int c = strcmp(cmd, root->cmd);
        if (c == 0)
            return root;
        root = (c < 0) ? root->left : root->right;
    }
    return NULL;
}

static void print_inorder(const Node *n, FILE *out, int *index)
{
    if (n == NULL)
        return;
    print_inorder(n->left, out, index);
    fprintf(out, "%2d. %s\n", ++*index, n->cmd);
    print_inorder(n->right, out, index);
}

static int bst_height(const Node *n)
{
    if (n == NULL)
        return 0;
    int l = bst_height(n->left), r = bst_height(n->right);
    return 1 + (l > r ? l : r);
}

static void free_tree(Node *n)
{
    if (n == NULL)
        return;
    free_tree(n->left);
    free_tree(n->right);
    free(n);
}

static void registry_clear(Registry *reg)
{
    free_tree(reg->root);
    reg->root = NULL;
    reg->count = 0;
}

static int edit_distance(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char d[MAX_CMD_LEN + 1][MAX_CMD_LEN + 1];

    assert(la <= MAX_CMD_LEN && lb <= MAX_CMD_LEN);

    for (size_t i = 0; i <= la; i++) d[i][0] = (unsigned char)i;
    for (size_t j = 0; j <= lb; j++) d[0][j] = (unsigned char)j;

    for (size_t i = 1; i <= la; i++) {
        int ca = toupper((unsigned char)a[i - 1]);
        for (size_t j = 1; j <= lb; j++) {
            int cb = toupper((unsigned char)b[j - 1]);
            int v = d[i - 1][j] + 1;
            int t = d[i][j - 1] + 1;
            if (t < v) v = t;
            t = d[i - 1][j - 1] + (ca != cb);
            if (t < v) v = t;
            if (i > 1 && j > 1 &&
                ca == toupper((unsigned char)b[j - 2]) &&
                toupper((unsigned char)a[i - 2]) == cb) {
                t = d[i - 2][j - 2] + 1;
                if (t < v) v = t;
            }
            d[i][j] = (unsigned char)v;
        }
    }
    return d[la][lb];
}

typedef struct {
    const char *cmd;
    int         dist;
} Closest;

static void closest_walk(const Node *n, const char *input, Closest *best)
{
    if (n == NULL)
        return;
    closest_walk(n->left, input, best);
    int d = edit_distance(input, n->cmd);
    if (d < best->dist) {
        best->dist = d;
        best->cmd = n->cmd;
    }
    closest_walk(n->right, input, best);
}

static const char *closest_command(const Node *root, const char *input)
{
    Closest best = { NULL, INT_MAX };
    closest_walk(root, input, &best);
    if (best.cmd == NULL)
        return NULL;

    size_t li = strlen(input), lc = strlen(best.cmd);
    size_t shorter = li < lc ? li : lc;
    int limit = (li >= 8) ? 2 : 1;

    if (best.dist <= limit && (size_t)best.dist < shorter)
        return best.cmd;
    return NULL;
}

static ReadStatus read_line(FILE *in, const char *prompt, char *buf, size_t size)
{
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

static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

static const char *command_text_error(const char *s)
{
    size_t n = strlen(s);
    if (n == 0)
        return "empty input";
    if (n > MAX_CMD_LEN)
        return "command too long";
    for (; *s != '\0'; s++)
        if (!isprint((unsigned char)*s))
            return "contains non-printable characters";
    return NULL;
}

static void timestamp_now(char *buf, size_t size)
{
    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    if (tmv == NULL || strftime(buf, size, "%Y-%m-%d %H:%M:%S", tmv) == 0)
        snprintf(buf, size, "unknown-time");
}

static void load_commands(Registry *reg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        printf("Warning: cannot open '%s': %s\n", path, strerror(errno));
        puts("Starting with an empty registry (all commands will be unknown).");
        return;
    }

    char line[INPUT_BUF];
    int line_no = 0, loaded = 0, dups = 0, bad = 0, overflow = 0, stop = 0;
    ReadStatus rs;

    while (!stop && (rs = read_line(f, NULL, line, sizeof line)) != READ_EOF) {
        line_no++;
        if (rs == READ_TOO_LONG) {
            printf("  line %d skipped: line too long\n", line_no);
            bad++;
            continue;
        }

        char *cmd = trim(line);
        if (*cmd == '\0')
            continue;

        const char *err = command_text_error(cmd);
        if (err != NULL) {
            printf("  line %d skipped: %s\n", line_no, err);
            bad++;
            continue;
        }

        switch (bst_insert(reg, cmd)) {
        case INSERT_OK:        loaded++;   break;
        case INSERT_DUPLICATE: dups++;     break;
        case INSERT_FULL:      overflow++; break;
        case INSERT_NO_MEMORY:
            puts("Error: out of memory while loading; stopping.");
            stop = 1;
            break;
        }
    }
    fclose(f);

    printf("Loaded %d approved command(s) from '%s'.\n", loaded, path);
    if (dups)
        printf("  %d duplicate line(s) ignored.\n", dups);
    if (bad)
        printf("  %d invalid line(s) skipped.\n", bad);
    if (overflow)
        printf("  Registry limit of %d reached: %d additional command(s) not loaded.\n",
               MAX_COMMANDS, overflow);
    if (loaded == 0)
        puts("Warning: no commands loaded; every command will be treated as unknown.");
}

static void audit_unknown(const char *cmd)
{
    FILE *f = fopen(AUDIT_FILE, "a");
    if (f == NULL) {
        printf("Warning: could not write to %s: %s\n", AUDIT_FILE, strerror(errno));
        return;
    }
    char ts[32];
    timestamp_now(ts, sizeof ts);
    fprintf(f, "%s | UNKNOWN | %s\n", ts, cmd);
    fclose(f);
}

static int save_session(const Registry *reg, const Session *s, const char *cmd_file)
{
    FILE *f = fopen(SESSION_FILE, "w");
    if (f == NULL)
        return -1;

    char ts[32];
    timestamp_now(ts, sizeof ts);
    fprintf(f, "Session ended:            %s\n", ts);
    fprintf(f, "Commands file:            %s\n", cmd_file);
    fprintf(f, "Approved at exit:         %d command(s)\n", reg->count);
    fprintf(f, "Verified (approved):      %d\n", s->approved);
    fprintf(f, "Not approved (suggested): %d\n", s->suggested);
    fprintf(f, "Not approved (unknown):   %d\n", s->unknown);
    fprintf(f, "Invalid inputs rejected:  %d\n", s->invalid);
    fputs("Registry at exit (alphabetical):\n", f);
    int idx = 0;
    print_inorder(reg->root, f, &idx);
    if (idx == 0)
        fputs("  (empty)\n", f);

    return fclose(f);
}

static void verify_command(const Registry *reg, Session *s)
{
    char buf[INPUT_BUF];
    ReadStatus rs = read_line(stdin, "Enter command: ", buf, sizeof buf);
    if (rs == READ_EOF) {
        putchar('\n');
        return;
    }
    if (rs == READ_TOO_LONG) {
        puts("Invalid input: command too long.");
        s->invalid++;
        return;
    }

    char *cmd = trim(buf);
    const char *err = command_text_error(cmd);
    if (err != NULL) {
        printf("Invalid input: %s.\n", err);
        s->invalid++;
        return;
    }

    if (bst_find(reg->root, cmd) != NULL) {
        puts("COMMAND APPROVED");
        s->approved++;
        return;
    }

    const char *hint = closest_command(reg->root, cmd);
    if (hint != NULL) {
        printf("Did you mean: %s?\n", hint);
        s->suggested++;
    } else {
        puts("UNKNOWN COMMAND");
        s->unknown++;
    }
    audit_unknown(cmd);
}

static void print_all(const Registry *reg)
{
    if (reg->root == NULL) {
        puts("Registry is empty.");
        return;
    }
    int idx = 0;
    print_inorder(reg->root, stdout, &idx);
}

static void print_help(void)
{
    puts("Commands: v=verify  p=print all  s=stats  x=clear registry  q=save & quit");
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : DEFAULT_CMD_FILE;
    Registry reg;
    Session sess = { 0, 0, 0, 0 };

    registry_init(&reg);
    load_commands(&reg, path);
    print_help();

    int running = 1;
    while (running) {
        char line[16];
        ReadStatus rs = read_line(stdin, "> ", line, sizeof line);
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

        switch (tolower((unsigned char)opt[0])) {
        case 'v':
            verify_command(&reg, &sess);
            break;
        case 'p':
            print_all(&reg);
            break;
        case 's':
            printf("Stored commands: %d / %d (tree height: %d)\n",
                   reg.count, MAX_COMMANDS, bst_height(reg.root));
            break;
        case 'x': {
            int removed = reg.count;
            registry_clear(&reg);
            printf("Registry cleared (%d command(s) removed).\n", removed);
            break;
        }
        case 'q':
            running = 0;
            break;
        default:
            puts("Unknown option.");
            print_help();
        }
    }

    if (save_session(&reg, &sess, path) == 0)
        printf("Session saved to %s.\n", SESSION_FILE);
    else
        printf("Warning: could not save session to %s.\n", SESSION_FILE);

    registry_clear(&reg);
    return 0;
}