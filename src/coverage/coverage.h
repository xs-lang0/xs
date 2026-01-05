// coverage.h -- line + branch coverage tracking
#ifndef XS_COVERAGE_H
#define XS_COVERAGE_H

typedef struct XSCoverage XSCoverage;

XSCoverage *coverage_new(const char *filename);
void        coverage_free(XSCoverage *c);
void        coverage_register_line(XSCoverage *c, int line);
void        coverage_record_line(XSCoverage *c, int line);
void        coverage_record_branch(XSCoverage *c, int line, int taken);
void        coverage_report(XSCoverage *c);

#endif
