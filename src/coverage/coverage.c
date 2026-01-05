#include "coverage/coverage.h"
#include "core/xs.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#define MAX_LINES    65536

typedef struct {
    int executable;
    int hit_count;
} LineCov;

typedef struct {
    int line;
    int taken;
    int not_taken;
} BranchCov;

struct XSCoverage {
    char     *filename;
    LineCov   lines[MAX_LINES];
    int       max_line;
    BranchCov branches[MAX_LINES];
    int       n_branches;
    int       total_lines;
    int       covered_lines;
    int       total_branches;
    int       covered_branches;
};

XSCoverage *coverage_new(const char *filename) {
    XSCoverage *c = calloc(1, sizeof(XSCoverage));
    if (!c) return NULL;
    c->filename = filename ? xs_strdup(filename) : NULL;
    c->max_line = 0;
    c->n_branches = 0;
    return c;
}

void coverage_free(XSCoverage *c) {
    if (!c) return;
    free(c->filename);
    free(c);
}

void coverage_register_line(XSCoverage *c, int line) {
    if (!c || line < 0 || line >= MAX_LINES) return;
    c->lines[line].executable = 1;
    if (line > c->max_line) {
        c->max_line = line;
    }
}

void coverage_record_line(XSCoverage *c, int line) {
    if (!c || line < 0 || line >= MAX_LINES) return;
    c->lines[line].executable = 1;
    c->lines[line].hit_count++;
    if (line > c->max_line) {
        c->max_line = line;
    }
}

void coverage_record_branch(XSCoverage *c, int line, int taken) {
    if (!c) return;

    for (int i = 0; i < c->n_branches; i++) {
        if (c->branches[i].line == line) {
            if (taken) {
                c->branches[i].taken = 1;
            } else {
                c->branches[i].not_taken = 1;
            }
            return;
        }
    }

    if (c->n_branches < MAX_LINES) {
        c->branches[c->n_branches].line = line;
        c->branches[c->n_branches].taken = taken ? 1 : 0;
        c->branches[c->n_branches].not_taken = taken ? 0 : 1;
        c->n_branches++;
    }
}

void coverage_report(XSCoverage *c) {
    if (!c) return;

    int total_executable = 0;
    int covered = 0;
    for (int i = 1; i <= c->max_line; i++) {
        if (c->lines[i].executable) {
            total_executable++;
            if (c->lines[i].hit_count > 0) {
                covered++;
            }
        }
    }

    if (total_executable == 0) {
        total_executable = 1;
    }

    c->covered_lines = covered;
    c->total_lines = total_executable;

    int total_br = c->n_branches;
    int covered_br = 0;
    for (int i = 0; i < c->n_branches; i++) {
        if (c->branches[i].taken && c->branches[i].not_taken) {
            covered_br++;
        }
    }
    c->total_branches = total_br;
    c->covered_branches = covered_br;

    fprintf(stderr, "\n=== XS Coverage Report ===\n");
    fprintf(stderr, "File: %s\n", c->filename ? c->filename : "<unknown>");

    double line_pct = c->total_lines > 0
        ? 100.0 * (double)c->covered_lines / (double)c->total_lines
        : 0.0;
    fprintf(stderr, "Lines:    %d/%d  (%.1f%%)\n",
        c->covered_lines, c->total_lines, line_pct);

    double branch_pct = c->total_branches > 0
        ? 100.0 * (double)c->covered_branches / (double)c->total_branches
        : 0.0;
    fprintf(stderr, "Branches: %d/%d  (%.1f%%)\n",
        c->covered_branches, c->total_branches, branch_pct);

    int uncovered_count = 0;
    for (int i = 1; i <= c->max_line; i++) {
        if (c->lines[i].executable && c->lines[i].hit_count == 0) {
            uncovered_count++;
        }
    }

    if (uncovered_count > 0) {
        fprintf(stderr, "\nUncovered lines:");
        int printed = 0;
        for (int i = 1; i <= c->max_line; i++) {
            if (c->lines[i].executable && c->lines[i].hit_count == 0) {
                if (printed > 0) fprintf(stderr, ",");
                fprintf(stderr, " %d", i);
                printed++;
                if (printed >= 30) {
                    fprintf(stderr, ", ...");
                    break;
                }
            }
        }
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "\n");
}
