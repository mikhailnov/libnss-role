#include <nss.h>
#include <grp.h>

#include <pthread.h>

#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const int STR_min_size  = 100;

static const int NO_SUCH_GROUP = -100;
static const int OUT_OF_RANGE  = -101;
static const int MEMORY_ERROR  = -102;
static const int UNKNOWN_ERROR = -103;
static const int OK            = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct ver {
	gid_t gid;
	gid_t *list;
	int size;
	int capacity;
};

typedef struct ver group_collector;

struct graph {
	struct ver *gr;
	int *used;
	int size;
	int capacity;
};

static int get_gid(char *, gid_t *);

static int graph_add(struct graph *G, struct ver v)
{
	if (G->size == G->capacity) {
		G->capacity <<= 1;
		G->gr = (struct ver *) realloc(G->gr, sizeof(struct ver) * G->capacity);
		if (!G->gr)
			return MEMORY_ERROR;
	}
	G->gr[G->size++] = v;
	return OK;
}

static int ver_add(struct ver *v, gid_t g)
{
	if (v->size == v->capacity) {
		v->capacity <<= 1;
		v->list = (gid_t *) realloc(v->list, sizeof(gid_t) * v->capacity);
		if (!v->list)
			return MEMORY_ERROR;
	}
	v->list[v->size++] = g;
	return OK;
}

static int graph_init(struct graph *G)
{
	G->gr = (struct ver *) malloc(sizeof(struct ver) * G->capacity);
	if (!G->gr)
		return MEMORY_ERROR;

	G->used = (int *) malloc(sizeof(int) * G->capacity);
	if (!G->used)
		return MEMORY_ERROR;

	memset(G->used, 0, sizeof(int) * G->capacity);
	return OK;
}

static int ver_init(struct ver *v)
{
	v->list = (gid_t *) malloc(sizeof(gid_t) * v->capacity);
	if (!v->list)
		return MEMORY_ERROR;

	return OK;
}

struct group_name {
	char name[32];
	int id;
};

static int find_id(struct graph *G, gid_t g, int *id)
{
	int i;
	for(i = 0; i < G->size; i++)
		if (G->gr[i].gid == g) {
			*id = i;
			return OK;
		}

	return NO_SUCH_GROUP;
}

static void free_all(struct graph *G)
{
	int i;
	for(i = 0; i < G->size; i++) {
		free(G->gr[i].list);
	}
	free(G->gr);
	free(G->used);
}

static int realloc_groups(long int **size, gid_t ***groups, long int new_size)
{
	gid_t *new_groups;

	new_groups = (gid_t *)
		realloc((**groups),
			new_size * sizeof(***groups));

	if (!new_groups)
		return MEMORY_ERROR;

	**groups = new_groups;
	**size = new_size;

	return OK;
}

static int parse_line(char *s, struct graph *G)
{
	int result;
	unsigned long len = strlen(s);
	int i;
	struct group_name role_name = {{0}, 0};
	struct ver role = {0, 0, 0, 10};

	result = ver_init(&role);
	if (result != OK)
		return result;

	for(i = 0; i < len; i++) {
		if (s[i] == ':') {
			i++;
			break;
		}
		role_name.name[role_name.id++] = s[i];
	}

	result = get_gid(role_name.name, &role.gid);
	if (result != OK)
		goto libnss_role_parse_line_error;

	while(1) {
		struct group_name gr_name = {{0}, 0};
		if (i >= len)
			break;
		gid_t gr;
		for(; i < len; i++) {
			if (s[i] == ',') {
				i++;
				break;
			}
			gr_name.name[gr_name.id++] = s[i];
		}

		result = get_gid(gr_name.name, &gr);
		if (result != OK && result != NO_SUCH_GROUP)
			goto libnss_role_parse_line_error;
		else if (result == NO_SUCH_GROUP)
			continue;

		result = ver_add(&role, gr);
		if (result != OK)
			goto libnss_role_parse_line_error;
	}

	result = graph_add(G, role);
	if (result != OK)
		goto libnss_role_parse_line_error;
	return result;

libnss_role_parse_line_error:
	free(role.list);
	return result;
}

static int reading(char *s, struct graph *G)
{
	int result;
	FILE *f = NULL;
	unsigned long len = STR_min_size;
	char *str = NULL;
	unsigned long id = 0;
	char c;

	f = fopen(s, "r");
	if (!f)
		goto libnss_role_reading_out;

	str = malloc(len * sizeof(char));
	if (!str)
		goto libnss_role_reading_close;

	while(1) {
		c = fgetc(f);
		if (c == EOF)
			break;
		if (c == '\n') {
			str[id] = '\0';
			result = parse_line(str, G);
			if (result != OK)
				goto libnss_role_reading_close;
			id = 0;
			continue;
		}
		str[id++] = c;
		if (id == len) {
			len <<= 1;
			str = realloc(str, len * sizeof(char));
			if (!str) {
				result = MEMORY_ERROR;
				goto libnss_role_reading_close;
			}
		}
	}
	if (id) {
		str[id] = '\0';
		result = parse_line(str, G);
		if (result != OK)
			goto libnss_role_reading_close;
	}
	
libnss_role_reading_close:
	fclose(f);
libnss_role_reading_out:
	free(str);
	return result;
}

static int dfs(struct graph *G, gid_t v, group_collector *col)
{
	int i, j, result;

	result = find_id(G, v, &i);
	if (result != OK) {
		result = ver_add(col, v);
		return result;
	}

	if (G->used[i])
		return OK;
	result = ver_add(col, v);
	if (result != OK)
		return result;
	G->used[i] = 1;

	for(j = 0; j < G->gr[i].size; j++) {
		result = dfs(G, G->gr[i].list[j], col);
		if (result != OK && result != NO_SUCH_GROUP)
			return result;
	}
	return OK;
}

static int get_gid(char *gr_name, gid_t *ans)
{
	if (!isdigit(gr_name[0])) {
		struct group grp, *grp_ptr;
		char buffer[1000];
		if (getgrnam_r(gr_name, &grp, buffer, 1000, &grp_ptr) == 0) {
			if (errno == ERANGE)
				return OUT_OF_RANGE;
			if (errno != 0)
				return UNKNOWN_ERROR;
		}
		if (!grp_ptr)
			return NO_SUCH_GROUP;
		*ans = grp.gr_gid;
		return OK;
	}

	if (sscanf(gr_name, "%u", ans) < 1)
		return UNKNOWN_ERROR;

	return OK;
}

enum nss_status _nss_role_initgroups_dyn(char *user, gid_t main_group,
		long int *start, long int *size, gid_t **groups,
		long int limit, int *errnop)
{
	enum nss_status ret = NSS_STATUS_SUCCESS;
	pthread_mutex_lock(&mutex);

	struct graph G = {0, 0, 0, 10};
	int i, result;
	group_collector col = {0, 0, 0, 10}, ans = {0, 0, 0, 10};

	result = graph_init(&G);
	if (result != OK) {
		*errnop = ENOMEM;
		ret = NSS_STATUS_NOTFOUND;
		goto libnss_role_out;
	}

	result = reading("/etc/role", &G);
	if (result != OK) {
		if (result == MEMORY_ERROR) {
			*errnop = ENOMEM;
			ret =  NSS_STATUS_NOTFOUND;
		} else
			ret = NSS_STATUS_UNAVAIL;
		goto libnss_role_out;
	}

	result = ver_init(&col);
	if (result != OK) {
		*errnop = ENOMEM;
		ret = NSS_STATUS_NOTFOUND;
		goto libnss_role_out;
	}

	result = dfs(&G, main_group, &col);
	if (result == MEMORY_ERROR) {
		*errnop = ENOMEM;
		ret = NSS_STATUS_NOTFOUND;
		goto libnss_role_out;
	}

	for(i = 0; i < *start; i++) {
		result = dfs(&G, (*groups)[i], &col);
		if (result == MEMORY_ERROR) {
			*errnop = ENOMEM;
			ret = NSS_STATUS_NOTFOUND;
			goto libnss_role_out;
		}
	}

	result = ver_init(&ans);
	if (result != OK) {
		*errnop = ENOMEM;
		ret = NSS_STATUS_NOTFOUND;
		goto libnss_role_out;
	}

	for(i = 0; i < col.size; i++) {
		int exist = 0, j;
		for(j = 0; j < *start; j++) {
			if ((*groups)[j] == col.list[i]) {
				exist = 1;
				break;
			}
		}
		if (main_group == col.list[i])
			exist = 1;
		for(j = 0; j < ans.size; j++) {
			if (ans.list[j] == col.list[i]) {
				exist = 1;
				break;
			}
		}

		if (exist)
			continue;

		result = ver_add(&ans, col.list[i]);
		if (result != OK) {
			*errnop = ENOMEM;
			ret = NSS_STATUS_NOTFOUND;
			goto libnss_role_out;
		}
	}

	if (*start + ans.size > *size) {
		if ((limit >= 0 && *start + ans.size > limit) ||
			realloc_groups(&size, &groups,
				*start + ans.size) != OK) {
			*errnop = ENOMEM;
			ret = NSS_STATUS_NOTFOUND;
			goto libnss_role_out;
		}
	}

	for(i = 0; i < ans.size; i++)
		(*groups)[(*start)++] = ans.list[i];

libnss_role_out:
	free(ans.list);
	free(col.list);
	free_all(&G);
	pthread_mutex_unlock(&mutex);
	return ret;
}
