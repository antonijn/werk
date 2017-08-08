#include "configfile.h"

#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <unistr.h>

#define HASH_BITS       8
#define NUM_BUCKETS     (1 << HASH_BITS)
#define HASH_MASK       (NUM_BUCKETS - 1)

static uint64_t hash_str(const char *str);
static void trim_space(const char **str, size_t *len);
static void ltrim_space(const char **str, size_t *len);
static void rtrim_space(const char *str, size_t *len);
static int parse_bool(const char *str, bool *value);

typedef struct kvp {
	uint64_t hash;
	const char *key;
	const char *value;
	int line;

	struct kvp *next;
} KVP;

struct config_file {
	KVP *buckets[NUM_BUCKETS];

	char category[1024];

	char *filename;

	int line;
	const char *error_msg;
};

static int config_read_line(ConfigFile *conf, const char *line);
static int config_insert_key(ConfigFile *conf,
                             const char *key,
                             size_t key_len,
                             const char *val,
                             size_t val_len);

ConfigFile *
config_read_path(const char *path)
{
	FILE *file = fopen(path, "r");
	ConfigFile *res = config_read(file, path);
	fclose(file);
	return res;
}

ConfigFile *
config_read(FILE *file, const char *filename)
{
	ConfigFile *conf = malloc(sizeof(ConfigFile));
	if (!conf)
		return NULL;

	char line[1024];

	memset(conf, 0, sizeof(ConfigFile));

	size_t fname_len = strlen(filename);
	conf->filename = malloc(fname_len + 1);
	if (!conf->filename) {
		conf->error_msg = "out of memory";
		return conf;
	}
	memcpy(conf->filename, filename, fname_len + 1);

	while (fgets(line, sizeof(line), file)) {
		++conf->line;

		if (config_read_line(conf, line))
			break;
	}

	return conf;
}

int
config_read_line(ConfigFile *conf, const char *line)
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
			conf->error_msg = "line must end in `]'";
			return -1;
		}

		++line;
		len -= 2;

		memcpy(conf->category, line, len);
		conf->category[len] = '\0';
		return 0;

	case '@':
		conf->error_msg = "line may not (yet) start with `@'";
		return -1;
	}

	const char *eq = strchr(line, '=');
	if (!eq) {
		conf->error_msg = "expected `='";
		return -1;
	}

	const char *pre_eq = line;
	const char *post_eq = eq + 1;
	size_t pre_eq_len = eq - pre_eq;
	size_t post_eq_len = len - pre_eq_len - 1;

	rtrim_space(pre_eq, &pre_eq_len);
	ltrim_space(&post_eq, &post_eq_len);

	if (pre_eq_len == 0) {
		conf->error_msg = "key name expected";
		return -1;
	}

	if (post_eq_len > 0 && post_eq[0] == '@') {
		conf->error_msg = "value may not (yet) start with `@`";
		return -1;
	}

	return config_insert_key(conf, pre_eq, pre_eq_len, post_eq, post_eq_len);
}

static int config_insert_key(ConfigFile *conf,
                             const char *key,
                             size_t key_len,
                             const char *val,
                             size_t val_len)
{
	size_t cat_len = strlen(conf->category);
	char *real_key = malloc(key_len + cat_len + 2);

	memcpy(real_key, conf->category, cat_len);
	real_key[cat_len] = '.';
	memcpy(real_key + cat_len + 1, key, key_len);
	real_key[cat_len + key_len + 1] = '\0';

	uint64_t hash = hash_str(real_key);
	uint64_t masked = hash & HASH_MASK;

	KVP *new_kvp = malloc(sizeof(KVP));
	if (!new_kvp) {
		conf->error_msg = "out of memory";
		return -1;
	}

	char *real_val = malloc(val_len + 1);
	if (!real_val) {
		conf->error_msg = "out of memory";
		return -1;
	}

	strncpy(real_val, val, val_len);
	real_val[val_len] = '\0';

	new_kvp->key = real_key;
	new_kvp->value = real_val;
	new_kvp->line = conf->line;
	new_kvp->hash = hash;
	new_kvp->next = conf->buckets[masked];

	conf->buckets[masked] = new_kvp;

	return 0;
}

bool
config_get(ConfigFile *conf, const char *key, const char **value, int *line)
{
	uint64_t hash = hash_str(key);
	uint64_t masked = hash & HASH_MASK;

	for (KVP *kvp = conf->buckets[masked]; kvp; kvp = kvp->next) {
		if (kvp->hash == hash && !strcmp(kvp->key, key)) {
			*value = kvp->value;
			*line = kvp->line;
			return true;
		}
	}

	return false;
}

bool
config_gets(ConfigFile *conf, const char *key, const char **value)
{
	int line;
	return config_get(conf, key, value, &line);
}

bool
config_geti(ConfigFile *conf, const char *key, int *value)
{
	const char *str_val;
	if (!config_gets(conf, key, &str_val))
		return false;

	*value = atoi(str_val);
	return true;
}

bool
config_getb(ConfigFile *conf, const char *key, bool *value)
{
	const char *str_val;
	int line;
	if (!config_get(conf, key, &str_val, &line))
		return false;

	if (parse_bool(str_val, value)) {
		fprintf(stderr,
		        "%s line %d: invalid boolean ``%s''\n",
		        conf->filename,
		        line,
		        str_val);

		return false;
	}

	return true;
}

bool
config_get_color(ConfigFile *conf, const char *key, RGB *value)
{
	const char *str;
	int line;
	if (!config_get(conf, key, &str, &line))
		return false;

	int r, g, b;
	if (sscanf(str, "rgb(%d, %d, %d)", &r, &g, &b) < 3) {
		fprintf(stderr, "%s line %d: expected rgb expression\n", conf->filename, line);
		return false;
	}

	if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
		fprintf(stderr, "%s line %d: invalid rgb expression\n", conf->filename, line);
		return false;
	}

	value->r = r;
	value->g = g;
	value->b = b;
	return true;
}

bool
config_get_switches(ConfigFile *conf,
                    const char *key,
                    const char *names[],
                    bool *values[])
{
	const char *str;
	int line;
	if (!config_get(conf, key, &str, &line))
		return false;

	bool bool_val;
	if (parse_bool(str, &bool_val) == 0) {
		for (int i = 0; names[i]; ++i)
			*values[i] = bool_val;
	}

	if (!strcmp(str, "all")) {
		for (int i = 0; names[i]; ++i)
			*values[i] = true;
		return true;
	}

	for (int i = 0; names[i]; ++i)
		*values[i] = false;

	if (!strcmp(str, "none"))
		return true;

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

	return true;
}

bool
config_get_error(ConfigFile *conf, int *line, const char **msg)
{
	if (conf->error_msg) {
		*msg = conf->error_msg;
		*line = conf->line;
		return true;
	}

	return false;
}

const char *
config_get_filename(ConfigFile *conf)
{
	return conf->filename;
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
