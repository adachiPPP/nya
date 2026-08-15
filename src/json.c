#include "nya.h"

typedef struct {
	const char *p;
	const char *end;
} jp;

static void j_ws(jp *j) {
	while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static json *j_new(int type) {
	json *n = xcalloc(1, sizeof *n);
	n->type = type;
	return n;
}

static json *j_parse_value(jp *j);

static char *j_parse_string_raw(jp *j) {
	if (j->p >= j->end || *j->p != '"') return NULL;
	j->p++;
	char *buf = xmalloc(64);
	size_t cap = 64, len = 0;
	while (j->p < j->end) {
		unsigned char c = *j->p++;
		if (c == '"') {
			buf[len] = '\0';
			return buf;
		}
		if (c == '\\') {
			if (j->p >= j->end) break;
			char e = *j->p++;
			char v = 0;
			switch (e) {
			case 'n': v = '\n'; break;
			case 't': v = '\t'; break;
			case 'r': v = '\r'; break;
			case 'b': v = '\b'; break;
			case 'f': v = '\f'; break;
			case '/': v = '/'; break;
			case '\\': v = '\\'; break;
			case '"': v = '"'; break;
			case 'u': {
				if (j->end - j->p >= 4) {
					unsigned int cp = 0;
					int i;
					for (i = 0; i < 4; i++) {
						char h = j->p[i];
						cp <<= 4;
						if (h >= '0' && h <= '9') cp |= h - '0';
						else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
						else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
					}
					j->p += 4;
					if (cp < 0x80) v = (char)cp;
					else if (cp < 0x800) {
						buf[len++] = (char)(0xc0 | (cp >> 6));
						v = (char)(0x80 | (cp & 0x3f));
					} else {
						buf[len++] = (char)(0xe0 | (cp >> 12));
						buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3f));
						v = (char)(0x80 | (cp & 0x3f));
					}
				}
				break;
			}
			default:
				v = e;
			}
			if (v) buf[len++] = v;
		} else {
			buf[len++] = (char)c;
		}
		if (len + 8 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
	}
	buf[len] = '\0';
	return buf;
}

static json *j_parse_string(jp *j) {
	char *s = j_parse_string_raw(j);
	if (!s) return NULL;
	json *n = j_new(J_STR);
	n->str = s;
	return n;
}

static json *j_parse_number(jp *j) {
	const char *start = j->p;
	while (j->p < j->end && (isdigit((unsigned char)*j->p) || *j->p == '-' || *j->p == '+' ||
	                         *j->p == '.' || *j->p == 'e' || *j->p == 'E')) j->p++;
	char *tmp = xstrndup(start, j->p - start);
	json *n = j_new(J_NUM);
	n->num = atof(tmp);
	free(tmp);
	return n;
}

static json *j_parse_array(jp *j) {
	json *n = j_new(J_ARR);
	j->p++;
	json **tail = &n->child;
	j_ws(j);
	if (j->p < j->end && *j->p == ']') {
		j->p++;
		return n;
	}
	for (;;) {
		j_ws(j);
		json *v = j_parse_value(j);
		if (!v) break;
		*tail = v;
		tail = &v->next;
		j_ws(j);
		if (j->p < j->end && *j->p == ',') {
			j->p++;
			continue;
		}
		if (j->p < j->end && *j->p == ']') {
			j->p++;
			break;
		}
		break;
	}
	return n;
}

static json *j_parse_object(jp *j) {
	json *n = j_new(J_OBJ);
	j->p++;
	json **tail = &n->child;
	j_ws(j);
	if (j->p < j->end && *j->p == '}') {
		j->p++;
		return n;
	}
	for (;;) {
		j_ws(j);
		char *key = j_parse_string_raw(j);
		if (!key) break;
		j_ws(j);
		if (j->p < j->end && *j->p == ':') j->p++;
		j_ws(j);
		json *v = j_parse_value(j);
		if (!v) {
			free(key);
			break;
		}
		v->key = key;
		*tail = v;
		tail = &v->next;
		j_ws(j);
		if (j->p < j->end && *j->p == ',') {
			j->p++;
			continue;
		}
		if (j->p < j->end && *j->p == '}') {
			j->p++;
			break;
		}
		break;
	}
	return n;
}

static json *j_parse_value(jp *j) {
	j_ws(j);
	if (j->p >= j->end) return NULL;
	char c = *j->p;
	if (c == '{') return j_parse_object(j);
	if (c == '[') return j_parse_array(j);
	if (c == '"') return j_parse_string(j);
	if (c == 't') {
		if (j->end - j->p >= 4 && strncmp(j->p, "true", 4) == 0) {
			j->p += 4;
			json *n = j_new(J_BOOL);
			n->num = 1;
			return n;
		}
		return NULL;
	}
	if (c == 'f') {
		if (j->end - j->p >= 5 && strncmp(j->p, "false", 5) == 0) {
			j->p += 5;
			json *n = j_new(J_BOOL);
			n->num = 0;
			return n;
		}
		return NULL;
	}
	if (c == 'n') {
		if (j->end - j->p >= 4 && strncmp(j->p, "null", 4) == 0) {
			j->p += 4;
			return j_new(J_NULL);
		}
		return NULL;
	}
	if (c == '-' || isdigit((unsigned char)c)) return j_parse_number(j);
	return NULL;
}

json *json_parse(const char *s, long len) {
	jp j;
	j.p = s;
	j.end = s + len;
	return j_parse_value(&j);
}

void json_free(json *j) {
	if (!j) return;
	json_free(j->child);
	json_free(j->next);
	free(j->key);
	free(j->str);
	free(j);
}

json *json_get(json *obj, const char *key) {
	json *n;
	for (n = obj ? obj->child : NULL; n; n = n->next) {
		if (n->key && strcmp(n->key, key) == 0) return n;
	}
	return NULL;
}

const char *json_str(json *j) {
	if (!j || j->type != J_STR) return NULL;
	return j->str;
}
