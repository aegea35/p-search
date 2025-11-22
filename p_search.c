#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>

static void write_all(int fd, const char* buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n <= 0) return;
        buf += n;
        len -= n;
    }
}

char* read_stdin_to_buffer(size_t* out_size) {
    size_t cap = 1024 * 1024; //1mb
    size_t size = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    ssize_t n;
    while ((n = read(STDIN_FILENO, buf + size, cap - size)) > 0) {
        size += n;
        if (size == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
    }
    *out_size = size;
    return buf;
}

static int match_literal(const char* line, size_t len, const char* kw, size_t kwlen, int ignore_case) {
    if (!ignore_case) return (len == kwlen && memcmp(line, kw, kwlen) == 0);
    for (size_t i = 0; i < kwlen && i < len; i++) if (tolower(line[i]) != tolower(kw[i])) return 0;
    return (len == kwlen);
}

static int is_word_char(char c) { return isalnum((unsigned char)c) || c == '_'; }
static int match_whole_word(const char* line, size_t len, const char* kw, size_t kwlen,int ignore_case) {
    for (size_t i = 0; i + kwlen <= len; i++) {
        if (ignore_case) {
            size_t j=0;
            for (; j<kwlen; j++) if (tolower(line[i+j]) != tolower(kw[j])) break;
            if (j != kwlen) continue;
        } else if (memcmp(line+i, kw, kwlen) != 0) continue;

        char before = (i == 0 ? '\n' : line[i-1]);
        char after  = (i+kwlen >= len ? '\n' : line[i+kwlen]);
        if (!is_word_char(before) && !is_word_char(after))return 1;
    }
    return 0;
}

// standard substring search
static int match_substring(const char* line, size_t len, const char* kw, size_t kwlen, int ignore_case) {
    if (!ignore_case) return (memmem(line, len, kw, kwlen) != NULL);
    for (size_t i = 0; i + kwlen <= len; i++) {
        size_t j = 0;
        for (; j < kwlen; j++) if (tolower(line[i+j]) != tolower(kw[j])) break;
        if (j == kwlen) return 1;
    }
    return 0;
}

void search_segment(const char* data, off_t start, off_t end, const char* keyword, size_t kwlen, int out_fd, size_t total_size, int ignore_case, int show_line_numbers, int whole_word, int literal){
    off_t pos = start;
    if (pos != 0 && pos < (off_t)total_size) while (pos < end && data[pos - 1] != '\n') pos++;
    if (pos >= end) return;
    size_t line_no = 1;
    for (off_t i = 0; i < pos; i++) if (data[i] == '\n') line_no++;

    while (pos < end && pos < (off_t)total_size) {
        off_t line_end = pos;
        while (line_end < (off_t)total_size && data[line_end] != '\n') line_end++;

        size_t L = line_end - pos;
        const char *line = data + pos;
        int matched = 0;
        if (literal) matched = match_literal(line, L, keyword, kwlen, ignore_case);
        else if (whole_word) matched = match_whole_word(line, L, keyword, kwlen, ignore_case);
        else matched = match_substring(line, L, keyword, kwlen, ignore_case);

        if (matched) {
            if (show_line_numbers) {
                char h[64];
                int hl = snprintf(h, sizeof(h), "%zu: ", line_no);
                write_all(out_fd, h, hl);
            }
            write_all(out_fd, line, L);
            write_all(out_fd, "\n", 1);
        }
        pos = line_end + 1;
        line_no++;
    }
}

int main(int argc, char *argv[]) {
    int ignore_case = 0;
    int show_line_numbers = 1;
    int whole_word = 0;
    int literal = 0;

    int argi = 1; // parsing the flags
    while (argi < argc && argv[argi][0] == '-') {
        const char *f = argv[argi] + 1;
        for (int i = 0; f[i]; i++) {
            switch (f[i]) {
                case 'i': ignore_case = 1; break;
                case 'n': show_line_numbers = !show_line_numbers; break;
                case 'w': whole_word = 1; break;
                case 'F': literal = 1; break;
                default: fprintf(stderr,"unknown flag: -%c\n", f[i]); exit(1);
            }
        }
        argi++;
    }

    if (argc - argi < 1) { fprintf(stderr,"usage: %s [flags] <keyword> [file] [workers]\n", argv[0]); exit(1); }
    const char *keyword_orig = argv[argi++];
    const char *filename = (argc-argi >= 1 ? argv[argi++] : NULL);
    int workers = (argc-argi >= 1 ? atoi(argv[argi++]) : sysconf(_SC_NPROCESSORS_ONLN));
    if (workers <= 0) workers = 4;
    size_t kwlen = strlen(keyword_orig);
    const char *kw_final = keyword_orig;
    char *kw_alloc = NULL;

    if (ignore_case) { //to lowercase if -i
        kw_alloc = strdup(keyword_orig);
        for (size_t i = 0; i < kwlen; i++) kw_alloc[i] = tolower(keyword_orig[i]);
        kw_final = kw_alloc;
    }

    char *data = NULL;
    size_t size = 0;
    int is_mmap = 0;
    if (filename && strcmp(filename, "-") != 0) { //file loading
        int fd = open(filename, O_RDONLY);
        if (fd < 0) { perror("open"); exit(1); }
        struct stat st;
        if (fstat(fd, &st) < 0) { perror("fstat"); exit(1); }
        size = st.st_size;
        data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (data == MAP_FAILED) { perror("mmap"); exit(1); }
        is_mmap = 1;
    } else {
        data = read_stdin_to_buffer(&size);
        if (!data) { fprintf(stderr,"stdin failed\n"); exit(1); }
        is_mmap = 0;
    }

    if (size == 0) exit(0);
    size_t total_lines = 0; //counting lines
    for (size_t i = 0; i < size; i++) if (data[i] == '\n') total_lines++;
    if (size && data[size-1] != '\n') total_lines++;

    if ((size_t)workers > total_lines) workers = total_lines;
    if (workers < 1) workers = 1;

    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); exit(1); }

    off_t chunk = size / workers;
    for (int w = 0; w < workers; w++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(1); }
        if (pid == 0) {
            close(pipefd[0]);
            off_t start = w * chunk;
            off_t end   = (w == workers - 1 ? size : (w+1) * (size_t) chunk);
            search_segment(data, start, end, kw_final, kwlen, pipefd[1], size, ignore_case, show_line_numbers, whole_word, literal);
            close(pipefd[1]);
            if (is_mmap) munmap(data, size); else free(data);
            if (kw_alloc) free(kw_alloc);
            exit(0);
        }
    }
    close(pipefd[1]);
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) write_all(STDOUT_FILENO, buf, n);
    close(pipefd[0]);
    while (wait(NULL) > 0);
    if (is_mmap) munmap(data, size); else free(data);
    if (kw_alloc) free(kw_alloc);

    return 0;
}
