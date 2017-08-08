#ifndef SPARSEF_H
#define SPARSEF_H

/*
 * Like sscanf, but fails more eagerly:
 *
 *   sscanf("10", "10 abc", &i)
 *
 * Succeeds, even though the source string didn't contain "abc".
 * sparsef() would fail.
 *
 * Currently supports `%d' and `%%' type specifiers.
 *
 * Returns -1 on failure, 0 on success.
 */
int sparsef(const char *str, const char *fmt, ...);

#endif
