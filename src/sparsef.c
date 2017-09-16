#include <werk/sparsef.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>

int
sparsef(const char *str, const char *fmt, ...)
{
	int ecode = 0;

	va_list ap;
	va_start(ap, fmt);

	int fmt_ch, str_ch;
	for (int i = 0; (fmt_ch = fmt[i]); ++i) {
		str_ch = *str;

		if (fmt_ch != '%') {
			if (str_ch != fmt_ch) {
				ecode = -1;
				goto stop;
			}

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
			if (errno == ERANGE) {
				ecode = -1;
				goto stop;
			}
			str = end;
			break;
			  }
		case '%': {
			if (str_ch != '%') {
				ecode = -1;
				goto stop;
			}
			++str;
			continue;
			  }
		}
	}

	if (*str)
		ecode = -1;

stop:
	va_end(ap);
	return ecode;
}
