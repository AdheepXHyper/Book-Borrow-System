/*
 * Book Borrow System (Data Structures project) - C version
 *
 * Data structures used:
 *  - Hash table (chaining)  : books and students, O(1) average lookup by ID
 *  - Min-heap               : active loans ordered by due date (earliest first)
 *  - Queue (linked list)    : FIFO waitlist per book
 *  - Linked list            : history of returned loans
 *
 * Compile: gcc library_system.c -o library
 * Run    : ./library
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOAN_DAYS    14
#define FINE_PER_DAY 2     /* currency units per late day */
#define MAX_LOANS    3     /* per student */
#define HASH_SIZE    101

/* ---------- Models ---------- */
typedef struct WaitNode {
    char sid[16];
    struct WaitNode *next;
} WaitNode;

typedef struct Book {
    char id[16], title[64], author[48];
    int total, available, waitCount;
    WaitNode *front, *rear;          /* waitlist queue */
    struct Book *next;               /* hash chain */
} Book;

typedef struct Student {
    char id[16], name[48];
    int activeCount;
    struct Student *next;            /* hash chain */
} Student;

typedef struct Loan {
    Book *book;
    Student *student;
    long issued, due, returned;      /* day numbers */
    int fine;
} Loan;

typedef struct HistNode {
    Loan *loan;
    struct HistNode *next;
} HistNode;

typedef struct {
    Loan **a;
    int n, cap;
} Heap;

/* ---------- Storage ---------- */
static Book    *btab[HASH_SIZE];
static Student *stab[HASH_SIZE];
static Heap     active = {NULL, 0, 0};
static HistNode *histHead = NULL, *histTail = NULL;

/* ---------- Dates (stored as days since 1970-01-01) ---------- */
static long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

static const char *fmt_date(long day) {
    static char bufs[4][16];
    static int idx = 0;
    char *b = bufs[idx++ % 4];
    int y, m, d;
    civil_from_days(day, &y, &m, &d);
    snprintf(b, 16, "%04d-%02d-%02d", y, m, d);
    return b;
}

static long today(void) {
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    return days_from_civil(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

static long parse_date(const char *s) {
    int y, m, d;
    if (s[0] == '\0') return today();
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) {
        printf("Bad date, using today.\n");
        return today();
    }
    return days_from_civil(y, m, d);
}

/* ---------- Input helpers ---------- */
static void ask(const char *label, char *buf, int n) {
    printf("%s: ", label);
    if (!fgets(buf, n, stdin)) { buf[0] = '\0'; return; }
    buf[strcspn(buf, "\r\n")] = '\0';
}

static long ask_date(void) {
    char t[128];
    ask("Date (yyyy-mm-dd, blank = today)", t, sizeof t);
    return parse_date(t);
}

static void copy_str(char *dst, const char *src, size_t n) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
}

/* ---------- Hash tables ---------- */
static unsigned hash_str(const char *s) {
    unsigned h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % HASH_SIZE;
}

static Book *find_book(const char *id) {
    for (Book *b = btab[hash_str(id)]; b; b = b->next)
        if (strcmp(b->id, id) == 0) return b;
    return NULL;
}

static Student *find_student(const char *id) {
    for (Student *s = stab[hash_str(id)]; s; s = s->next)
        if (strcmp(s->id, id) == 0) return s;
    return NULL;
}

/* ---------- Min-heap of loans (by due date) ---------- */
static void hswap(Loan **x, Loan **y) { Loan *t = *x; *x = *y; *y = t; }

static void sift_up(Heap *h, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->a[p]->due <= h->a[i]->due) break;
        hswap(&h->a[p], &h->a[i]);
        i = p;
    }
}

static void sift_down(Heap *h, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < h->n && h->a[l]->due < h->a[m]->due) m = l;
        if (r < h->n && h->a[r]->due < h->a[m]->due) m = r;
        if (m == i) break;
        hswap(&h->a[m], &h->a[i]);
        i = m;
    }
}

static void heap_push(Heap *h, Loan *l) {
    if (h->n == h->cap) {
        h->cap = h->cap ? h->cap * 2 : 16;
        h->a = realloc(h->a, h->cap * sizeof(Loan *));
        if (!h->a) { puts("Out of memory."); exit(1); }
    }
    h->a[h->n] = l;
    sift_up(h, h->n++);
}

static Loan *heap_pop(Heap *h) {
    Loan *top = h->a[0];
    h->a[0] = h->a[--h->n];
    sift_down(h, 0);
    return top;
}

static void heap_remove_at(Heap *h, int i) {
    h->a[i] = h->a[--h->n];
    if (i < h->n) { sift_up(h, i); sift_down(h, i); }
}

static Heap heap_clone(const Heap *h) {
    Heap c;
    c.n = c.cap = h->n;
    c.a = malloc((h->n ? h->n : 1) * sizeof(Loan *));
    if (h->n) memcpy(c.a, h->a, h->n * sizeof(Loan *));
    return c;
}

/* ---------- Waitlist queue ---------- */
static int wait_pos(Book *b, const char *sid) {
    int pos = 1;
    for (WaitNode *w = b->front; w; w = w->next, pos++)
        if (strcmp(w->sid, sid) == 0) return pos;
    return 0;
}

static void wait_add(Book *b, const char *sid) {
    WaitNode *w = malloc(sizeof(WaitNode));
    copy_str(w->sid, sid, sizeof w->sid);
    w->next = NULL;
    if (b->rear) b->rear->next = w; else b->front = w;
    b->rear = w;
    b->waitCount++;
}

static void wait_remove(Book *b, const char *sid) {
    WaitNode *prev = NULL;
    for (WaitNode *w = b->front; w; prev = w, w = w->next) {
        if (strcmp(w->sid, sid) == 0) {
            if (prev) prev->next = w->next; else b->front = w->next;
            if (w == b->rear) b->rear = prev;
            free(w);
            b->waitCount--;
            return;
        }
    }
}

/* ---------- Printing ---------- */
static void print_loan(const Loan *l) {
    printf("%-6s %-24s -> %-10s issued:%s due:%s",
           l->book->id, l->book->title, l->student->name,
           fmt_date(l->issued), fmt_date(l->due));
    if (l->returned)
        printf(" returned:%s fine:%d", fmt_date(l->returned), l->fine);
    putchar('\n');
}

/* ---------- Operations ---------- */
static void add_book(const char *id, const char *title, const char *author, int copies) {
    if (copies <= 0) { puts("Copies must be a positive number."); return; }
    Book *b = find_book(id);
    if (b) {
        b->total += copies;
        b->available += copies;
        printf("Added %d more copies of %s\n", copies, b->title);
        return;
    }
    b = calloc(1, sizeof(Book));
    copy_str(b->id, id, sizeof b->id);
    copy_str(b->title, title, sizeof b->title);
    copy_str(b->author, author, sizeof b->author);
    b->total = b->available = copies;
    unsigned h = hash_str(b->id);
    b->next = btab[h];
    btab[h] = b;
    puts("Book added.");
}

static void add_student(const char *id, const char *name) {
    if (find_student(id)) { puts("Student ID already exists."); return; }
    Student *s = calloc(1, sizeof(Student));
    copy_str(s->id, id, sizeof s->id);
    copy_str(s->name, name, sizeof s->name);
    unsigned h = hash_str(s->id);
    s->next = stab[h];
    stab[h] = s;
    puts("Student registered.");
}

static void issue_book(const char *bid, const char *sid, long date) {
    Book *b = find_book(bid);
    Student *s = find_student(sid);
    if (!b || !s) { puts("Invalid book or student ID."); return; }
    if (s->activeCount >= MAX_LOANS) {
        printf("Limit reached: %s already has %d books.\n", s->name, MAX_LOANS);
        return;
    }
    for (int i = 0; i < active.n; i++)
        if (active.a[i]->book == b && active.a[i]->student == s) {
            puts("Student already has this book.");
            return;
        }

    if (b->available == 0) {
        int pos = wait_pos(b, sid);
        if (!pos) { wait_add(b, sid); pos = b->waitCount; }
        printf("No copies left. %s is on the waitlist (position %d).\n", s->name, pos);
        return;
    }
    /* if a waitlist exists, only the student at the front may borrow */
    if (b->front && strcmp(b->front->sid, sid) != 0) {
        puts("Copy reserved for the next student on the waitlist.");
        return;
    }
    wait_remove(b, sid);

    Loan *l = calloc(1, sizeof(Loan));
    l->book = b;
    l->student = s;
    l->issued = date;
    l->due = date + LOAN_DAYS;
    b->available--;
    s->activeCount++;
    heap_push(&active, l);
    printf("Issued. Due date: %s\n", fmt_date(l->due));
}

static void return_book(const char *bid, const char *sid, long date) {
    Student *s = find_student(sid);
    if (!s) { puts("Invalid student ID."); return; }

    int idx = -1;
    for (int i = 0; i < active.n; i++)
        if (active.a[i]->student == s && strcmp(active.a[i]->book->id, bid) == 0) {
            idx = i;
            break;
        }
    if (idx < 0) { puts("No such active loan."); return; }

    Loan *l = active.a[idx];
    heap_remove_at(&active, idx);

    long late = date - l->due;
    if (late < 0) late = 0;
    l->returned = date;
    l->fine = (int)(late * FINE_PER_DAY);
    s->activeCount--;
    l->book->available++;

    HistNode *h = malloc(sizeof(HistNode));
    h->loan = l;
    h->next = NULL;
    if (histTail) histTail->next = h; else histHead = h;
    histTail = h;

    if (late > 0) printf("Returned. LATE by %ld day(s). Fine: %d\n", late, l->fine);
    else          puts("Returned. On time, no fine.");

    if (l->book->front) {
        Student *nx = find_student(l->book->front->sid);
        if (nx) printf("Notify %s: '%s' is now available.\n", nx->name, l->book->title);
    }
}

static void show_books(void) {
    int any = 0;
    for (int i = 0; i < HASH_SIZE; i++)
        for (Book *b = btab[i]; b; b = b->next) {
            printf("%-6s %-26s %-16s %d/%d available  waitlist:%d\n",
                   b->id, b->title, b->author, b->available, b->total, b->waitCount);
            any = 1;
        }
    if (!any) puts("No books.");
}

static void show_active(void) {
    if (active.n == 0) { puts("No active loans."); return; }
    Heap c = heap_clone(&active);
    while (c.n > 0) print_loan(heap_pop(&c));   /* due-date order */
    free(c.a);
}

static void show_overdue(long asOf) {
    Heap c = heap_clone(&active);
    int any = 0;
    while (c.n > 0 && c.a[0]->due < asOf) {
        Loan *l = heap_pop(&c);
        long late = asOf - l->due;
        print_loan(l);
        printf("   -> %ld days late, fine so far: %ld\n", late, late * FINE_PER_DAY);
        any = 1;
    }
    if (!any) printf("No overdue books as of %s\n", fmt_date(asOf));
    free(c.a);
}

static void show_history(void) {
    if (!histHead) { puts("No history yet."); return; }
    for (HistNode *h = histHead; h; h = h->next) print_loan(h->loan);
}

/* ---------- Main ---------- */
int main(void) {
    char a[128], b[128], c[128], d[128];

    /* sample data */
    add_book("B1", "Data Structures in C", "Tanenbaum", 2);
    add_book("B2", "Algorithms", "Sedgewick", 1);
    add_student("S1", "Asha");
    add_student("S2", "Ravi");

    for (;;) {
        puts("\n=== BOOK BORROW SYSTEM ===");
        puts("1 Add book      2 Register student   3 Issue book");
        puts("4 Return book   5 Show books         6 Active loans");
        puts("7 Overdue list  8 History            0 Exit");
        ask("Choice", a, sizeof a);

        switch (a[0]) {
        case '1':
            ask("Book ID", a, sizeof a);
            ask("Title", b, sizeof b);
            ask("Author", c, sizeof c);
            ask("Copies", d, sizeof d);
            add_book(a, b, c, atoi(d));
            break;
        case '2':
            ask("Student ID", a, sizeof a);
            ask("Name", b, sizeof b);
            add_student(a, b);
            break;
        case '3':
            ask("Book ID", a, sizeof a);
            ask("Student ID", b, sizeof b);
            issue_book(a, b, ask_date());
            break;
        case '4':
            ask("Book ID", a, sizeof a);
            ask("Student ID", b, sizeof b);
            return_book(a, b, ask_date());
            break;
        case '5': show_books(); break;
        case '6': show_active(); break;
        case '7': show_overdue(ask_date()); break;
        case '8': show_history(); break;
        case '0': puts("Bye!"); return 0;
        default:  puts("Invalid choice.");
        }
    }
}
