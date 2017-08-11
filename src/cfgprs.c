#include "cfgprs.h"
#include "sparsef.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <unistr.h>

#define HASH_BITS       8
#define NUM_BUCKETS     (1 << HASH_BITS)
#define HASH_MASK       (NUM_BUCKETS - 1)
#define MAX_LINE_SIZE   1024

static uint64_t hash_str(const char *str);
static void trim_space(const char **str, size_t *len);
static void ltrim_space(const char **str, size_t *len);
static void rtrim_space(const char *str, size_t *len);
static int parse_bool(const char *str, bool *value);

typedef struct allocation {
	struct allocation *next;
	void *mem;
} Allocation;

typedef struct kvp {
	uint64_t hash;
	const char *key;

	option_callback callback;
	void *udata;

	struct kvp *next;
} KVP;

struct config_reader {
	KVP *buckets[NUM_BUCKETS];
	char category[MAX_LINE_SIZE];

	Allocation *allocations;
	const char *filename;
	int line;
};

static int config_read_line(ConfigReader *conf, const char *line);
static KVP *config_get_callback(ConfigReader *conf, const char *key);
static int config_call_option(ConfigReader *conf,
                              const char *key,
                              size_t key_len,
                              const char *val,
                              size_t val_len);

void
config_report(ConfigReader *conf, const char *fmt, ...)
{
	fprintf(stderr, "%s line %d: ", conf->filename, conf->line);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

ConfigReader *
config_init(void)
{
	ConfigReader *conf = malloc(sizeof(ConfigReader));
	if (!conf)
		return NULL;

	memset(conf, 0, sizeof(ConfigReader));
	return conf;
}

void
config_read_file(ConfigReader *conf, const char *path)
{
	conf->filename = path;

	FILE *file = fopen(path, "r");
	if (!file) {
		config_report(conf, "opening file `%s' failed\n", path);
		return;
	}

	conf->line = 0;

	char line[MAX_LINE_SIZE];
	while (fgets(line, sizeof(line), file)) {
		size_t line_length = strlen(line);
		if (line_length == 0)
			continue;

		++conf->line;

		if (line[line_length - 1] != '\n') {
			config_report(conf, "line too long");

			int ch;
			do {
				ch = fgetc(file);
			} while (ch != '\n' && ch != EOF);

			continue;
		}

		if (config_read_line(conf, line))
			break;
	}

	fclose(file);
}

int
config_read_line(ConfigReader *conf, const char *line)
{
	size_t len = strlen(line);
	trim_space(&line, &len);

	if (len == 0)
		return 0;

	switch (line[0]) {
	case '#':
		return 0;

	case '[':
		if (line[len - 1] != ']') {
			config_report(conf, "line must end in `]'\n");
			return 0;
		}

		++line;
		len -= 2;

		memcpy(conf->category, line, len);
		conf->category[len] = '\0';
		return 0;

	case '@':
		config_report(conf, "line may not (yet) start with `@'\n");
		return 0;
	}

	const char *eq = strchr(line, '=');
	if (!eq) {
		config_report(conf, "expected `='\n");
		return 0;
	}

	const char *pre_eq = line;
	const char *post_eq = eq + 1;
	size_t pre_eq_len = eq - pre_eq;
	size_t post_eq_len = len - pre_eq_len - 1;

	rtrim_space(pre_eq, &pre_eq_len);
	ltrim_space(&post_eq, &post_eq_len);

	if (pre_eq_len == 0) {
		config_report(conf, "key name expected\n");
		return -1;
	}

	if (post_eq_len > 0 && post_eq[0] == '@') {
		config_report(conf, "value may not (yet) start with `@`\n");
		return -1;
	}

	return config_call_option(conf, pre_eq, pre_eq_len, post_eq, post_eq_len);
}

static int
config_call_option(ConfigReader *conf,
                   const char *key,
                   size_t key_len,
                   const char *val,
                   size_t val_len)
{
	size_t cat_len = strlen(conf->category);
	char *real_key = malloc(key_len + cat_len + 2);
	if (!real_key) {
		config_report(conf, "out of memory\n");
		return -1;
	}

	memcpy(real_key, conf->category, cat_len);
	real_key[cat_len] = '.';
	memcpy(real_key + cat_len + 1, key, key_len);
	real_key[cat_len + key_len + 1] = '\0';

	KVP *kvp = config_get_callback(conf, real_key);
	if (!kvp) {
		config_report(conf, "no such option `%s'\n", real_key);

		free(real_key);
		return 0;
	}

	free(real_key);

	char *real_val = malloc(val_len + 1);
	if (!real_val) {
		config_report(conf, "out of memory\n");
		return -1;
	}

	memcpy(real_val, val, val_len);
	real_val[val_len] = '\0';

	kvp->callback(conf, real_val, kvp->udata);

	free(real_val);
	return 0;
}

static KVP *
config_get_callback(ConfigReader *conf, const char *key)
{
	uint64_t hash = hash_str(key);
	uint64_t masked = hash & HASH_MASK;

	for (KVP *kvp = conf->buckets[masked]; kvp; kvp = kvp->next)
		if (kvp->hash == hash && !strcmp(kvp->key, key))
			return kvp;

	return NULL;
}

void
config_add_opt(ConfigReader *conf, const char *key, option_callback opt, void *udata)
{
	uint64_t hash = hash_str(key);
	uint64_t masked = hash & HASH_MASK;

	KVP *nkvp = malloc(sizeof(KVP));
	if (!nkvp) {
		config_report(conf, "out of memory\n");
		return;
	}

	memset(nkvp, 0, sizeof(*nkvp));
	nkvp->key = key;
	nkvp->hash = hash;
	nkvp->callback = opt;
	nkvp->udata = udata;
	nkvp->next = conf->buckets[masked];

	conf->buckets[masked] = nkvp;
}

static void
opt_s_callback(ConfigReader *conf, const char *str, void *udata)
{
	const char **value = udata;

	size_t strl = strlen(str);
	char *dup = malloc(strl + 1);
	if (!dup) {
		config_report(conf, "out of memory\n");
		return;
	}

	memcpy(dup, str, strl + 1);
	*value = dup;
}

void
config_add_opt_s(ConfigReader *conf, const char *key, const char **value)
{
	config_add_opt(conf, key, opt_s_callback, value);
}

static void
opt_b_callback(ConfigReader *conf, const char *str, void *udata)
{
	if (parse_bool(str, udata))
		config_report(conf, "invalid boolean `%s'\n", str);
}

void
config_add_opt_b(ConfigReader *conf, const char *key, bool *value)
{
	config_add_opt(conf, key, opt_b_callback, value);
}

static void
opt_i_callback(ConfigReader *conf, const char *str, void *udata)
{
	int *value = udata;

	long res = strtol(str, NULL, 0);
	if (errno == ERANGE || (long)(int)res != res) {
		config_report(conf, "invalid integer (or too big): `%s'\n", str);
		return;
	}

	*value = res;
}

void
config_add_opt_i(ConfigReader *conf, const char *key, int *value)
{
	config_add_opt(conf, key, opt_i_callback, value);
}

/*
 * Magic algo pulled off the internet 😅
 */
static RGB
hsv_to_rgb(int h_i, int s_i, int v_i)
{
	RGB res;

	double r, g, b;

	double h = h_i;
	double s = s_i / 100.0;
	double v = v_i / 100.0;

	if (s == 0) {
		r = g = b = v;
		goto done;
	}

	double hh = h / 60.0;

	int i = (int)hh;
	double ff = hh - i;
	double p = v * (1.0 - s);
	double q = v * (1.0 - (s * ff));
	double t = v * (1.0 - (s * (1.0 - ff)));

	switch (i) {
	case 0:
		r = v;
		g = t;
		b = p;
		break;

	case 1:
		r = q;
		g = v;
		b = p;
		break;

	case 2:
		r = p;
		g = v;
		b = t;
		break;

	case 3:
		r = p;
		g = q;
		b = v;
		break;

	case 4:
		r = t;
		g = p;
		b = v;
		break;

	case 5:
		r = v;
		g = p;
		b = q;
		break;
	}

done:
	res.r = (int)(r * 255);
	res.g = (int)(g * 255);
	res.b = (int)(b * 255);
	return res;
}

static void
opt_color_callback(ConfigReader *conf, const char *str, void *udata)
{
	RGB *value = udata;

	int h, s, v;
	if (!sparsef(str, "hsv(%d, %d, %d)", &h, &s, &v)) {
		if (h < 0 || h >= 360 || s < 0 || s > 100 || v < 0 || v > 100) {
			config_report(conf, "invalid hsv expression `%s'\n", str);
			return;
		}

		*value = hsv_to_rgb(h, s, v);
		return;
	}

	int r, g, b;
	if (!sparsef(str, "rgb(%d, %d, %d)", &r, &g, &b)) {
		if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
			config_report(conf, "invalid rgb expression `%s'\n", str);
			return;
		}

		value->r = r;
		value->g = g;
		value->b = b;
		return;
	}

	config_report(conf, "invalid color `%s'\n", str);
}

void
config_add_opt_color(ConfigReader *conf, const char *key, RGB *value)
{
	config_add_opt(conf, key, opt_color_callback, value);
}

typedef struct {
	const char **names;
	bool **values;
} NamesValues;

static void
opt_flags_callback(ConfigReader *conf, const char *str, void *udata)
{
	NamesValues *nvs = udata;
	const char **names = nvs->names;
	bool **values = nvs->values;

	bool bool_val;
	if (parse_bool(str, &bool_val) == 0) {
		for (int i = 0; names[i]; ++i)
			*values[i] = bool_val;
	}

	if (!strcmp(str, "all")) {
		for (int i = 0; names[i]; ++i)
			*values[i] = true;
		return;
	}

	for (int i = 0; names[i]; ++i)
		*values[i] = false;

	if (!strcmp(str, "none"))
		return;

	const char *field = str;
	size_t field_len;

	for (;;) {
		const char *nxt_field = strchr(field, ',');
		if (!nxt_field)
			nxt_field = strchr(str, '\0');

		field_len = nxt_field - field;
		trim_space(&field, &field_len);

		if (field_len == 0)
			break;

		/* horribly inefficient... */
		bool found = false;
		for (int i = 0; names[i]; ++i) {
			if (!strncmp(field, names[i], field_len)) {
				*values[i] = true;
				found = true;
				break;
			}
		}

		if (!found)
			fprintf(stderr, "%s line %d: unknown switch ``%*s''\n", field_len, field);

		if (!nxt_field[0])
			break;

		field = nxt_field + 1;
	}
}

void
config_add_opt_flags(ConfigReader *conf,
                     const char *key,
                     const char *names[],
                     bool *values[])
{
	int num_names;
	for (num_names = 0; names[num_names]; ++num_names)
		;

	size_t values_size = num_names * sizeof(bool *);
	bool **alloc_vals = config_alloc(conf, values_size);

	NamesValues *nvs = config_alloc(conf, sizeof(NamesValues));

	if (!nvs || !alloc_vals) {
		config_report(conf, "out of memory\n");
		free(nvs);
		free(alloc_vals);
		return;
	}

	memcpy(alloc_vals, values, values_size);

	nvs->names = names;
	nvs->values = alloc_vals;

	config_add_opt(conf, key, opt_flags_callback, nvs);
}

int
parse_bool(const char *str, bool *value)
{
	if (!strcmp(str, "true") || !strcmp(str, "yes") || !strcmp(str, "1") || !strcmp(str, u8"✓")) {
		*value = true;
		return 0;
	}

	if (!strcmp(str, "false") || !strcmp(str, "no") || !strcmp(str, "0") || !strcmp(str, u8"✗")) {
		*value = false;
		return 0;
	}

	return -1;
}

static uint64_t
hash_str(const char *str)
{
	/* FNV-1a hash */

	const uint64_t fnv_prime = (1UL << 40) + (1UL << 8) + 0xB3UL;
	const uint64_t offset_basis = 14695981039346656037UL;

	uint64_t hash = offset_basis;
	char ch;
	for (int i = 0; str[i]; ++i)
		hash = (hash ^ str[i]) * fnv_prime;

	return hash;
}

static void
trim_space(const char **str, size_t *len)
{
	ltrim_space(str, len);
	rtrim_space(*str, len);
}
static void
ltrim_space(const char **str, size_t *len)
{
	const char *s = *str;
	size_t l = *len;
	ucs4_t cp;

	const char *move, *stop = s + l;
	while (s != stop) {
		move = u8_next(&cp, s);
		if (!uc_is_c_whitespace(cp))
			break;

		l -= move - s;
		s = move;
	}

	*str = s;
	*len = l;
}
static void
rtrim_space(const char *str, size_t *len)
{
	size_t l = *len;
	ucs4_t cp;

	const char *move = str + l, *nxt;
	while (move != str) {
		nxt = u8_prev(&cp, move, str);
		if (!uc_is_c_whitespace(cp))
			break;

		l -= move - nxt;
		move = nxt;
	}

	*len = l;
}

void
config_destroy(ConfigReader *conf)
{
	if (!conf)
		return;

	for (int i = 0; i < NUM_BUCKETS; ++i) {
		for (KVP *k = conf->buckets[i]; k; ) {
			KVP *nxt = k->next;
			free(k);
			k = nxt;
		}
	}

	for (Allocation *alloc = conf->allocations; alloc; ) {
		Allocation *nxt = alloc->next;
		free(alloc->mem);
		alloc = nxt;
	}

	free(conf);
}

void *
config_alloc(ConfigReader *conf, size_t size)
{
	Allocation *alloc = malloc(sizeof(Allocation));
	void *mem = malloc(size);
	if (!alloc || !mem) {
		config_report(conf, "out of memory\n");
		free(alloc);
		free(mem);
		return NULL;
	}

	alloc->mem = mem;
	alloc->next = conf->allocations;
	conf->allocations = alloc;

	return mem;
}
