/* Configuration helpers: dynamic arrays, string, color, layout, modifier and key parsing. */
#ifndef SOLUX_CONFIG_CONFIG_UTIL_H
#define SOLUX_CONFIG_CONFIG_UTIL_H

static void
dnx_oom(void)
{
	fprintf(stderr, "solux: out of memory while growing DNX configuration\n");
	exit(EXIT_FAILURE);
}

static void *
dnx_realloc_array(void *ptr, size_t *cap, size_t len, size_t item_size)
{
	size_t newcap = *cap ? *cap : 16;
	void *p;

	while (newcap < len) {
		if (newcap > SIZE_MAX / 2)
			newcap = len;
		else
			newcap *= 2;
		if (newcap < len && newcap == SIZE_MAX)
			dnx_oom();
	}
	if (newcap > SIZE_MAX / item_size)
		dnx_oom();
	p = realloc(ptr, newcap * item_size);
	if (!p)
		dnx_oom();
	*cap = newcap;
	return p;
}

static void
dnx_init_dynamic_config(void)
{
	rules_cap = LENGTH(default_rules);
	rules = calloc(rules_cap ? rules_cap : 1, sizeof(*rules));
	if (!rules)
		dnx_oom();
	memcpy(rules, default_rules, LENGTH(default_rules) * sizeof(*rules));
	rules_len = LENGTH(default_rules);

	monrules_cap = LENGTH(default_monrules);
	monrules = calloc(monrules_cap ? monrules_cap : 1, sizeof(*monrules));
	if (!monrules)
		dnx_oom();
	memcpy(monrules, default_monrules, LENGTH(default_monrules) * sizeof(*monrules));
	monrules_len = LENGTH(default_monrules);

	/* Keys/buttons/gestures come exclusively from config.py. */
	keys = NULL;
	keys_len = keys_cap = 0;
	buttons = NULL;
	buttons_len = buttons_cap = 0;
	gestures = NULL;
	gestures_len = gestures_cap = 0;
}

#define DEF_CFG "default/config.py"
#define USR_CFG "~/.config/solux/config.py"
#define SOLUX_STANDARD_CFG "/etc/solux/config.py"

static const char *dnx_config_override;

static char *dnx_strdup(const char *s)
{
	size_t n;
	char *p;

	if (!s)
		return NULL;
	n = strlen(s) + 1;
	p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

static char *dnx_strdup_or_null(const char *s)
{
	return dnx_strdup(s);
}

static size_t dnx_tags_owned;

static void
dnx_reset_tags(void)
{
	static const char *default_tags[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9"
	};
	size_t i;

	for (i = 0; i < dnx_tags_owned; i++)
		free(tags[i]);

	for (i = 0; i <= MAX_TAGS; i++)
		tags[i] = NULL;

	for (i = 0; i < LENGTH(default_tags); i++) {
		tags[i] = dnx_strdup(default_tags[i]);
		if (!tags[i])
			dnx_oom();
	}
	dnx_tags_owned = LENGTH(default_tags);
	tagcount = LENGTH(default_tags);
}

static char *dnx_unquote_value(const char *s)
{
	char *p, *r;
	size_t n;

	if (!s)
		return NULL;
	p = dnx_strdup(s);
	if (!p)
		return NULL;
	n = strlen(p);
	if (n >= 2 && ((p[0] == '"' && p[n - 1] == '"') ||
			(p[0] == '\'' && p[n - 1] == '\''))) {
		p[n - 1] = '\0';
		r = p + 1;
		memmove(p, r, strlen(r) + 1);
	}
	return p;
}

static uint32_t
dnx_hex_color(const char *s, uint32_t fallback)
{
	char *end;
	unsigned long v;

	if (!s)
		return fallback;
	while (isspace((unsigned char)*s))
		s++;
	if (!strncasecmp(s, "0x", 2))
		s += 2;
	else if (*s == '#')
		s++;
	if (!*s)
		return fallback;
	errno = 0;
	v = strtoul(s, &end, 16);
	if (errno || end == s)
		return fallback;
	while (isspace((unsigned char)*end))
		end++;
	if (*end)
		return fallback;
	return (uint32_t)v;
}

static void
dnx_set_rgba(float dst[4], const char *s)
{
	float v[4];
	int n = 0;
	const char *p = s;
	char *end;

	if (!s)
		return;
	while (n < 4) {
		while (isspace((unsigned char)*p) || *p == ',')
			p++;
		if (!*p)
			break;
		v[n] = strtof(p, &end);
		if (end == p)
			break;
		p = end;
		if (*p == 'f' || *p == 'F')
			p++;
		n++;
	}
	if (n == 4) {
		for (int i = 0; i < 4; i++)
			dst[i] = v[i];
	}
}

static void
dnx_set_hex_rgba(float dst[4], const char *s)
{
	uint32_t c = dnx_hex_color(s, 0);
	dst[0] = ((c >> 24) & 0xff) / 255.0f;
	dst[1] = ((c >> 16) & 0xff) / 255.0f;
	dst[2] = ((c >> 8) & 0xff) / 255.0f;
	dst[3] = (c & 0xff) / 255.0f;
}

static int
dnx_is_null(const char *s)
{
	return !s || !strcasecmp(s, "null") || !strcasecmp(s, "none");
}

static int
dnx_layout_index(const char *s)
{
	char *end;
	long n;
	char name[128];
	const char *p, *q;
	size_t len;

	if (!s)
		return -1;

	/* Numeric layout indexes are accepted as before. */
	n = strtol(s, &end, 0);
	if (end != s && !*end && n >= 0 && n < (long)layouts_len)
		return (int)n;

	

	p = s;
	while (isspace((unsigned char)*p))
		p++;
	q = p + strlen(p);
	while (q > p && isspace((unsigned char)q[-1]))
		q--;
	if (q > p && *p == '[' && q[-1] == ']') {
		p++;
		q--;
		while (q > p && isspace((unsigned char)q[-1]))
			q--;
		while (*p && isspace((unsigned char)*p))
			p++;
	}
	len = (size_t)(q - p);
	if (len >= sizeof(name))
		len = sizeof(name) - 1;
	memcpy(name, p, len);
	name[len] = '\0';

	for (size_t i = 0; i < layouts_len; i++) {
		if (layouts[i].symbol && !strcasecmp(layouts[i].symbol, s))
			return (int)i;

		/* Match the normalized name against the displayed symbol too. */
		if (layouts[i].symbol) {
			const char *sp = layouts[i].symbol;
			while (isspace((unsigned char)*sp))
				sp++;
			if (*sp == '[') {
				sp++;
				while (isspace((unsigned char)*sp))
					sp++;
			}
			q = sp + strlen(sp);
			while (q > sp && isspace((unsigned char)q[-1]))
				q--;
			if (q > sp && q[-1] == ']')
				q--;
			while (q > sp && isspace((unsigned char)q[-1]))
				q--;
			len = (size_t)(q - sp);
			if (len == strlen(name) && !strncasecmp(sp, name, len))
				return (int)i;
		}
	}

	return -1;
}

static void (*dnx_arrange_for_name(const char *name))(Monitor *)
{
	if (dnx_is_null(name) || !strcasecmp(name, "float"))
		return NULL;
	if (!strcasecmp(name, "dwindle")) return dwindle;
	if (!strcasecmp(name, "tile")) return tile;
	if (!strcasecmp(name, "monocle")) return monocle;
	return NULL;
}

static enum wl_output_transform
dnx_transform(const char *s)
{
	if (!s) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "WL_OUTPUT_TRANSFORM_NORMAL")) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "NORMAL")) return WL_OUTPUT_TRANSFORM_NORMAL;
	if (!strcasecmp(s, "90")) return WL_OUTPUT_TRANSFORM_90;
	if (!strcasecmp(s, "180")) return WL_OUTPUT_TRANSFORM_180;
	if (!strcasecmp(s, "270")) return WL_OUTPUT_TRANSFORM_270;
	if (!strcasecmp(s, "FLIPPED")) return WL_OUTPUT_TRANSFORM_FLIPPED;
	if (!strcasecmp(s, "FLIPPED_90")) return WL_OUTPUT_TRANSFORM_FLIPPED_90;
	if (!strcasecmp(s, "FLIPPED_180")) return WL_OUTPUT_TRANSFORM_FLIPPED_180;
	if (!strcasecmp(s, "FLIPPED_270")) return WL_OUTPUT_TRANSFORM_FLIPPED_270;
	return WL_OUTPUT_TRANSFORM_NORMAL;
}

static uint32_t
dnx_mods(const char *s)
{
	uint32_t m = 0;
	char *tmp, *p, *save;

	if (!s || !*s || !strcasecmp(s, "NONE"))
		return 0;
	tmp = dnx_strdup(s);
	if (!tmp)
		return 0;
	for (p = strtok_r(tmp, "|+,", &save); p; p = strtok_r(NULL, "|+,", &save)) {
		while (isspace((unsigned char)*p)) p++;
		if (!strcasecmp(p, "MODKEY"))
			m |= MODKEY;
		else if (!strcasecmp(p, "MODSUPER") || !strcasecmp(p, "SUPER"))
			m |= MODSUPER;
		else if (!strcasecmp(p, "LOGO"))
			m |= MODSUPER;
		else if (!strcasecmp(p, "MODALT"))
			m |= MODALT;
		else if (!strcasecmp(p, "MODCTRL"))
			m |= MODCTRL;
		else if (!strcasecmp(p, "MODNONE"))
			m |= MODNONE;
		else if (!strcasecmp(p, "SHIFT"))
			m |= WLR_MODIFIER_SHIFT;
		else if (!strcasecmp(p, "CTRL") || !strcasecmp(p, "CONTROL"))
			m |= WLR_MODIFIER_CTRL;
		else if (!strcasecmp(p, "ALT") || !strcasecmp(p, "MOD1"))
			m |= WLR_MODIFIER_ALT;
		else if (!strcasecmp(p, "MOD2"))
			m |= WLR_MODIFIER_MOD2;
		else if (!strcasecmp(p, "MOD3"))
			m |= WLR_MODIFIER_MOD3;
		else if (!strcasecmp(p, "MOD4"))
			m |= WLR_MODIFIER_LOGO;
		else if (!strcasecmp(p, "MOD5"))
			m |= WLR_MODIFIER_MOD5;
		else
			m |= (uint32_t)strtoul(p, NULL, 0);
	}
	free(tmp);
	return m;
}

static xkb_keysym_t
dnx_keysym(const char *s)
{
	xkb_keysym_t k;
	char *tmp;

	if (!s) return XKB_KEY_NoSymbol;
	tmp = dnx_unquote_value(s);
	if (!tmp) return XKB_KEY_NoSymbol;
	if (!strncasecmp(tmp, "XKB_KEY_", 8))
		memmove(tmp, tmp + 8, strlen(tmp + 8) + 1);
	k = xkb_keysym_from_name(tmp, XKB_KEYSYM_CASE_INSENSITIVE);
	if (k == XKB_KEY_NoSymbol) {
		char *end;
		unsigned long n = strtoul(tmp, &end, 0);
		if (end != tmp && !*end)
			k = (xkb_keysym_t)n;
	}
	free(tmp);
	return k;
}



static int
dnx_bool_value(const char *s, int fallback)
{
	if (!s)
		return fallback;
	if (!strcasecmp(s, "1") || !strcasecmp(s, "true") ||
			!strcasecmp(s, "yes") || !strcasecmp(s, "on"))
		return 1;
	if (!strcasecmp(s, "0") || !strcasecmp(s, "false") ||
			!strcasecmp(s, "no") || !strcasecmp(s, "off"))
		return 0;
	return atoi(s) != 0;
}

#endif
