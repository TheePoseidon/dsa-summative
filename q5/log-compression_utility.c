# include <ctype.h>
# include <errno.h>
# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>

# define INPUT_FILE "device_log.txt"
# define COMPRESSED_FILE "device_log.huff"
# define RESTORED_FILE "device_log_restored.txt"

# define ALPHABET   256
# define MAX_NODES  (2 * ALPHABET - 1)
# define MAGIC "HUF1"
# define MAGIC_LEN  4
# define HEADER_LEN (MAGIC_LEN + 4)

typedef struct HNode {
    uint64_t freq;
    unsigned order;
    int symbol;
    struct HNode *left;
    struct HNode *right;
} HNode;

static HNode *new_node(uint64_t freq, int symbol, unsigned order,
                       HNode *left, HNode *right){
    HNode *n = malloc(sizeof *n);
    if (n == NULL)
        return NULL;
    n->freq = freq;
    n->order = order;
    n->symbol = symbol;
    n->left = left;
    n->right = right;
    return n;
}

static int is_leaf(const HNode *n){
    return n->left == NULL;
}

static void free_tree(HNode *n)
{
    if (n == NULL)
        return;
    free_tree(n->left);
    free_tree(n->right);
    free(n);
}

typedef struct {
    HNode *item[ALPHABET];
    int n;
} Heap;

static int node_less(const HNode *a, const HNode *b)
{
    if (a->freq != b->freq)
        return a->freq < b->freq;
    return a->order < b->order;
}

static void heap_push(Heap *h, HNode *x){
    int i = h->n++;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!node_less(x, h->item[parent]))
            break;
        h->item[i] = h->item[parent];
        i = parent;
    }
    h -> item[i] = x;
}

static HNode *heap_pop(Heap *h){
    HNode *top = h->item[0];
    HNode *last = h->item[--h->n];
    if (h->n > 0) {
        int i = 0;
        for (;;) {
            int c = 2 * i + 1;
            if (c >= h->n)
                break;
            if (c + 1 < h->n && node_less(h->item[c + 1], h->item[c]))
                c++;
            if (!node_less(h->item[c], last))
                break;
            h->item[i] = h->item[c];
            i = c;
        }
        h->item[i] = last;
    }
    return top;
}

static HNode *build_tree(const uint64_t freq[ALPHABET])
{
    Heap h;
    h.n = 0;
    unsigned next_order = ALPHABET;

    for (int s = 0; s < ALPHABET; s++) {
        if (freq[s] == 0)
            continue;
        HNode *leaf = new_node(freq[s], s, (unsigned)s, NULL, NULL);
        if (leaf == NULL)
            goto fail;
        heap_push(&h, leaf);
    }
    if (h.n == 0)
        return NULL;

    while (h.n > 1) {
        HNode *a = heap_pop(&h);
        HNode *b = heap_pop(&h);
        HNode *parent = new_node(a -> freq + b->freq, -1, next_order++, a, b);
        if (parent == NULL) {
            free_tree(a);
            free_tree(b);
            goto fail;
        }
        heap_push(&h, parent);
    }
    return heap_pop(&h);

fail:
    while (h.n > 0)
        free_tree(heap_pop(&h));
    return NULL;
}

typedef struct {
    char bits[ALPHABET];
    int  len;
} Code;

static void assign_codes(const HNode *n, char *path, int depth, Code *table){
    if (is_leaf(n)) {
        Code *c = &table[n->symbol];
        if (depth == 0) {
            c->bits[0] = '0';
            c->bits[1] = '\0';
            c->len = 1;
        } else {
            memcpy(c->bits, path, (size_t)depth);
            c->bits[depth] = '\0';
            c->len = depth;
        }
        return;
    }
    path[depth] = '0';
    assign_codes(n->left, path, depth + 1, table);
    path[depth] = '1';
    assign_codes(n->right, path, depth + 1, table);
}

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
    unsigned cur;
    int nbits;
    int failed;
} BitWriter;

static void bw_put_bit(BitWriter *w, int bit)
{
    if (w->failed)
        return;
    w->cur = (w->cur << 1) | (unsigned)(bit & 1);
    if (++w->nbits < 8)
        return;

    if (w->len == w->cap) {
        size_t ncap = w->cap ? w->cap * 2 : 256;
        uint8_t *p = realloc(w->buf, ncap);
        if (p == NULL) {
            w->failed = 1;
            return;
        }
        w->buf = p;
        w->cap = ncap;
    }
    w->buf[w->len++] = (uint8_t)w->cur;
    w->cur = 0;
    w->nbits = 0;
}

static void bw_put_byte(BitWriter *w, unsigned value)
{
    for (int i = 7; i >= 0; i--)
        bw_put_bit(w, (int)((value >> i) & 1u));
}

static void bw_flush(BitWriter *w)
{
    while (!w->failed && w->nbits != 0)
        bw_put_bit(w, 0);
}

static uint64_t bw_bits(const BitWriter *w)
{
    return (uint64_t)w->len * 8 + (uint64_t)w->nbits;
}

typedef struct {
    const uint8_t *buf;
    uint64_t total;
    uint64_t pos;
} BitReader;

static int br_get(BitReader *r){
    if (r->pos >= r->total)
        return -1;
    int bit = (r->buf[r->pos >> 3] >> (7 - (r->pos & 7))) & 1;
    r->pos++;
    return bit;
}

static void write_tree(BitWriter *w, const HNode *n){
    if (is_leaf(n)) {
        bw_put_bit(w, 1);
        bw_put_byte(w, (unsigned)n->symbol);
        return;
    }
    bw_put_bit(w, 0);
    write_tree(w, n->left);
    write_tree(w, n->right);
}

typedef enum { TREE_OK, TREE_TRUNCATED, TREE_INVALID, TREE_NOMEM } TreeStatus;

typedef struct {
    BitReader *r;
    int nodes;
    unsigned char seen[ALPHABET];
} TreeParser;

static HNode *read_tree(TreeParser *p, int depth, TreeStatus *st)
{
    if (depth >= ALPHABET || ++p->nodes > MAX_NODES) {
        *st = TREE_INVALID;
        return NULL;
    }

    int bit = br_get(p->r);
    if (bit < 0) {
        *st = TREE_TRUNCATED;
        return NULL;
    }

    if (bit == 1) {
        int sym = 0;
        for (int i = 0; i < 8; i++) {
            int b = br_get(p->r);
            if (b < 0) {
                *st = TREE_TRUNCATED;
                return NULL;
            }
            sym = (sym << 1) | b;
        }
        if (p->seen[sym]) {
            *st = TREE_INVALID;
            return NULL;
        }
        p->seen[sym] = 1;
        HNode *leaf = new_node(0, sym, 0, NULL, NULL);
        if (leaf == NULL)
            *st = TREE_NOMEM;
        return leaf;
    }

    HNode *left = read_tree(p, depth + 1, st);
    if (left == NULL)
        return NULL;
    HNode *right = read_tree(p, depth + 1, st);
    if (right == NULL) {
        free_tree(left);
        return NULL;
    }
    HNode *n = new_node(0, -1, 0, left, right);
    if (n == NULL) {
        free_tree(left);
        free_tree(right);
        *st = TREE_NOMEM;
    }
    return n;
}

typedef enum { DEC_OK, DEC_BAD_MAGIC, DEC_TRUNCATED, DEC_CORRUPT, DEC_NOMEM } DecStatus;

static DecStatus decode_buffer(const uint8_t *in, size_t n, uint8_t **out, size_t *outlen){
    *out = NULL;
    *outlen = 0;

    if (n < MAGIC_LEN || memcmp(in, MAGIC, MAGIC_LEN) != 0)
        return DEC_BAD_MAGIC;
    if (n < HEADER_LEN)
        return DEC_TRUNCATED;

    uint32_t orig = (uint32_t)in[4] | ((uint32_t)in[5] << 8) |
                    ((uint32_t)in[6] << 16) | ((uint32_t)in[7] << 24);

    if (orig == 0)
        return (n == HEADER_LEN) ? DEC_OK : DEC_CORRUPT;

    BitReader r = { in + HEADER_LEN, (uint64_t)(n - HEADER_LEN) * 8, 0 };
    TreeParser tp;
    tp.r = &r;
    tp.nodes = 0;
    memset(tp.seen, 0, sizeof tp.seen);
    TreeStatus st = TREE_OK;

    HNode *root = read_tree(&tp, 0, &st);
    if (root == NULL)
        return st == TREE_NOMEM ? DEC_NOMEM
             : st == TREE_TRUNCATED ? DEC_TRUNCATED : DEC_CORRUPT;
    if ((uint64_t)orig > r.total - r.pos) {
        free_tree(root);
        return DEC_TRUNCATED;
    }

    uint8_t *buf = malloc(orig);
    if (buf == NULL) {
        free_tree(root);
        return DEC_NOMEM;
    }

    int single = is_leaf(root);
    DecStatus result = DEC_OK;
    for (uint32_t i = 0; i < orig; i++) {
        const HNode *node = root;
        if (single) {
            if (br_get(&r) < 0) {
                result = DEC_TRUNCATED;
                break;
            }
        } else {
            while (!is_leaf(node)) {
                int b = br_get(&r);
                if (b < 0) {
                    result = DEC_TRUNCATED;
                    break;
                }
                node = b ? node->right : node->left;
            }
            if (result != DEC_OK)
                break;
        }
        buf[i] = (uint8_t)node->symbol;
    }
    free_tree(root);

    if (result == DEC_OK && (r.pos + 7) / 8 != r.total / 8)
        result = DEC_CORRUPT;

    if (result != DEC_OK) {
        free(buf);
        return result;
    }
    *out = buf;
    *outlen = orig;
    return DEC_OK;
}

static int load_file(const char *path, uint8_t **data, size_t *len, const char *missing_hint){
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("Error: cannot open '%s': %s\n", path, strerror(errno));
        if (errno == ENOENT && missing_hint != NULL)
            printf("%s\n", missing_hint);
        return 0;
    }

    size_t cap = 4096, n = 0;
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        fclose(f);
        puts("Error: out of memory.");
        return 0;
    }

    for (;;) {
        if (n == cap) {
            uint8_t *p = realloc(buf, cap * 2);
            if (p == NULL) {
                free(buf);
                fclose(f);
                puts("Error: out of memory (file too large).");
                return 0;
            }
            buf = p;
            cap *= 2;
        }
        size_t got = fread(buf + n, 1, cap - n, f);
        n += got;
        if ((uint64_t)n > UINT32_MAX) {
            free(buf);
            fclose(f);
            printf("Error: '%s' is larger than 4 GB, which this format cannot store.\n", path);
            return 0;
        }
        if (got == 0)
            break;
    }

    if (ferror(f)) {
        printf("Error: could not read '%s'.\n", path);
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *data = buf;
    *len = n;
    return 1;
}

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        printf("Error: cannot create '%s': %s\n", path, strerror(errno));
        return 0;
    }
    int ok = (len == 0 || fwrite(data, 1, len, f) == len);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        printf("Error: failed while writing '%s'.\n", path);
    return ok;
}

typedef struct {
    int      sym;
    uint64_t freq;
} Entry;

static int entry_cmp(const void *pa, const void *pb){
    const Entry *a = pa, *b = pb;
    if (a->freq != b->freq)
        return a->freq < b->freq ? 1 : -1;
    return a->sym - b->sym;
}

static void char_label(int c, char *out, size_t size)
{
    switch (c) {
    case '\n': snprintf(out, size, "'\\n'"); return;
    case '\r': snprintf(out, size, "'\\r'"); return;
    case '\t': snprintf(out, size, "'\\t'"); return;
    default:
        if (c >= 0x20 && c <= 0x7E)
            snprintf(out, size, "'%c'", c);
        else
            snprintf(out, size, "0x%02X", c);
    }
}

static void print_table(const uint64_t freq[ALPHABET], const Code *codes, size_t total)
{
    Entry e[ALPHABET];
    int k = 0;
    uint64_t bits = 0;
    for (int s = 0; s < ALPHABET; s++) {
        if (freq[s] == 0)
            continue;
        e[k].sym = s;
        e[k].freq = freq[s];
        k++;
        bits += freq[s] * (uint64_t)codes[s].len;
    }
    qsort(e, (size_t)k, sizeof e[0], entry_cmp);

    printf("\nCharacter frequencies and Huffman codes:\n");
    printf("%-10s %10s   %s\n", "Character", "Frequency", "Code");
    for (int i = 0; i < k; i++) {
        char label[16];
        char_label(e[i].sym, label, sizeof label);
        printf("%-10s %10llu   %s\n", label, (unsigned long long)e[i].freq,
               codes[e[i].sym].bits);
    }
    printf("\n%d distinct characters, %zu total. Average code length: %.2f bits/character.\n",
           k, total, (double)bits / (double)total);
}

static void compress_log(void)
{
    uint8_t *data = NULL;
    size_t n = 0;
    if (!load_file(INPUT_FILE, &data, &n, "Place the diagnostic log in this directory first."))
        return;
    if (n == 0) {
        printf("'%s' is empty; there is nothing to compress.\n", INPUT_FILE);
        free(data);
        return;
    }

    HNode *root = NULL;
    Code *codes = NULL;
    BitWriter w;
    memset(&w, 0, sizeof w);

    uint64_t freq[ALPHABET];
    memset(freq, 0, sizeof freq);
    for (size_t i = 0; i < n; i++)
        freq[data[i]]++;

    root = build_tree(freq);
    codes = calloc(ALPHABET, sizeof *codes);
    if (root == NULL || codes == NULL) {
        puts("Error: out of memory while building the Huffman tree.");
        goto done;
    }

    char path[ALPHABET];
    assign_codes(root, path, 0, codes);
    print_table(freq, codes, n);

    for (int i = 0; i < MAGIC_LEN; i++)
        bw_put_byte(&w, (unsigned char)MAGIC[i]);
    for (int i = 0; i < 4; i++)
        bw_put_byte(&w, (unsigned)(((uint32_t)n >> (8 * i)) & 0xFFu));

    uint64_t mark = bw_bits(&w);
    write_tree(&w, root);
    uint64_t tree_bits = bw_bits(&w) - mark;

    mark = bw_bits(&w);
    for (size_t i = 0; i < n; i++)
        for (const char *c = codes[data[i]].bits; *c != '\0'; c++)
            bw_put_bit(&w, *c == '1');
    uint64_t data_bits = bw_bits(&w) - mark;
    bw_flush(&w);

    if (w.failed) {
        puts("Error: out of memory while encoding.");
        goto done;
    }
    if (!write_file(COMPRESSED_FILE, w.buf, w.len))
        goto done;

    double ratio = 100.0 * (double)w.len / (double)n;
    printf("\nLog compressed and saved to %s\n", COMPRESSED_FILE);
    printf("Original size:     %zu bytes\n", n);
    printf("Compressed size:   %zu bytes\n", w.len);
    printf("Compression ratio: %.2f%% (compressed / original)\n", ratio);
    printf("Space saved:       %.2f%%\n", 100.0 - ratio);
    printf("Layout: %d-byte header + %llu-bit tree + %llu-bit data (+ padding)\n",
           HEADER_LEN, (unsigned long long)tree_bits, (unsigned long long)data_bits);
    if (w.len >= n)
        puts("Note: this log is too small for Huffman coding to pay off; the stored "
             "tree costs more than the encoding saves.");

done:
    free_tree(root);
    free(codes);
    free(w.buf);
    free(data);
}
static void verify_log(void){
    FILE *a = fopen(INPUT_FILE, "rb");
    if (a == NULL) {
        printf("Cannot verify: unable to open '%s': %s\n", INPUT_FILE, strerror(errno));
        return;
    }
    FILE *b = fopen(RESTORED_FILE, "rb");
    if (b == NULL) {
        printf("Cannot verify: unable to open '%s': %s\n", RESTORED_FILE, strerror(errno));
        if (errno == ENOENT)
            puts("Run option 2 (Decompress log) first.");
        fclose(a);
        return;
    }

    unsigned char ba[4096], bb[4096];
    int same = 1, io_error = 0;
    for (;;) {
        size_t na = fread(ba, 1, sizeof ba, a);
        size_t nb = fread(bb, 1, sizeof bb, b);
        if (ferror(a) || ferror(b)) {
            io_error = 1;
            break;
        }
        if (na != nb || memcmp(ba, bb, na) != 0) {
            same = 0;
            break;
        }
        if (na == 0)
            break;
    }
    fclose(a);
    fclose(b);

    if (io_error)
        puts("Cannot verify: a read error occurred while comparing the files.");
    else if (same)
        puts("Verification: SUCCESS\nThe restored file is identical to the original.");
    else
        puts("Verification: FAILED\nThe restored file differs from the original.");
}

static void decompress_log(void)
{
    uint8_t *in = NULL, *out = NULL;
    size_t in_len = 0, out_len = 0;

    if (!load_file(COMPRESSED_FILE, &in, &in_len, "Run option 1 (Compress log) first."))
        return;

    DecStatus st = decode_buffer(in, in_len, &out, &out_len);
    free(in);

    switch (st) {
    case DEC_OK:
        break;
    case DEC_BAD_MAGIC:
        printf("Error: '%s' is not a valid compressed log (bad header).\n", COMPRESSED_FILE);
        return;
    case DEC_TRUNCATED:
        printf("Error: '%s' is truncated or damaged (data ends early).\n", COMPRESSED_FILE);
        return;
    case DEC_CORRUPT:
        printf("Error: '%s' is corrupt (invalid tree or unexpected extra data).\n", COMPRESSED_FILE);
        return;
    case DEC_NOMEM:
    default:
        puts("Error: out of memory while decompressing.");
        return;
    }

    int ok = write_file(RESTORED_FILE, out, out_len);
    free(out);
    if (!ok)
        return;

    printf("Decompressed %zu bytes and saved to %s\n\n", out_len, RESTORED_FILE);
    verify_log();
}

typedef enum { READ_OK, READ_EOF, READ_TOO_LONG } ReadStatus;

static ReadStatus read_line(const char *prompt, char *buf, size_t size){
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

static char *trim(char *s){
    while (*s != '\0' && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

static void print_menu(void){
    puts("\n=== Diagnostic Log Compressor (Huffman) ===");
    puts("1. Compress log");
    puts("2. Decompress log");
    puts("3. Verify restored log");
    puts("4. Exit");
}

int main(void){
    for (;;) {
        print_menu();
        char line[16];
        ReadStatus rs = read_line("Enter choice: ", line, sizeof line);
        if (rs == READ_EOF) {
            putchar('\n');
            break;
        }

        char *opt = trim(line);
        if (rs == READ_TOO_LONG || opt[0] == '\0' || opt[1] != '\0') {
            puts("Invalid choice. Enter a number from 1 to 4.");
            continue;
        }

        int done = 0;
        switch (opt[0]) {
        case '1': compress_log();   break;
        case '2': decompress_log(); break;
        case '3': verify_log();     break;
        case '4': done = 1;         break;
        default:  puts("Invalid choice. Enter a number from 1 to 4.");
        }
        if (done)
            break;
    }
    puts("Goodbye.");
    return 0;
}