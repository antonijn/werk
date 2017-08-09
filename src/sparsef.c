#include "sparsef.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

int
sparsef(const char *str, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	int fmt_ch, str_ch;
	for (int i = 0; (fmt_ch = fmt[i]); ++i) {
		str_ch = *str;

		if (fmt_ch != '%') {
			if (str_ch != fmt_ch)
				return -1;

			++str;
			continue;
		}

		fmt_ch = fmt[++i];
		switch (fmt_ch) {
		case 'x':
		case 'd': {
			int *argp = va_arg(ap, int *);
			char *end;
			int base = (fmt_ch == 'x') ? 16 : 10;
			*argp = strtol(str, &end, base);
			if (errno == ERANGE)
				return -1;
			str = end;
			break;
			  }
		case '%': {
			if (str_ch != '%')
				return -1;
			++str;
			continue;
			  }
		}
	}

	if (*str)
		return -1;

	return 0;
}
