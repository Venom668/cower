#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <yajl/yajl_tree.h>

#include "macro.h"
#include "package.h"

static void free_strv(char **strv) {
  char **s;

  if (strv == NULL)
    return;

  for (s = strv; *s; ++s) {
    free(*s);
  }

  free(strv);
}

void aur_package_free(aurpkg_t *package) {
  if (package == NULL) {
    return;
  }

  free(package->name);
  free(package->description);
  free(package->maintainer);
  free(package->pkgbase);
  free(package->upstream_url);
  free(package->aur_urlpath);
  free(package->version);

  free_strv(package->licenses);
  free_strv(package->conflicts);
  free_strv(package->depends);
  free_strv(package->groups);
  free_strv(package->makedepends);
  free_strv(package->optdepends);
  free_strv(package->checkdepends);
  free_strv(package->provides);
  free_strv(package->replaces);
  free_strv(package->keywords);

  free(package);
}

void aur_packages_free(aurpkg_t **packages) {
  aurpkg_t **p;

  if (packages == NULL) {
    return;
  }

  for (p = packages; *p; ++p) {
    aur_package_free(*p);
  }

  free(packages);
}

static void aur_packages_freep(aurpkg_t ***packages) {
  aur_packages_free(*packages);
}

#define _cleanup_packages_free_ __attribute__((cleanup(aur_packages_freep)))

struct json_descriptor_t {
  const char *key;
  yajl_type type;
  size_t offset;
};

static int json_map_key_cmp(const void *a, const void *b) {
  const struct json_descriptor_t *j = a, *k = b;

  return strcmp(j->key, k->key);
}

static const struct json_descriptor_t *descmap_get_key(const struct json_descriptor_t table[], size_t tabsize, const char *key) {
  struct json_descriptor_t needle = { .key = key };

  return bsearch(&needle, table, tabsize, sizeof(struct json_descriptor_t), json_map_key_cmp);
}

static int copy_to_string(yajl_val node, char **s) {
  *s = strdup(node->u.string);
  if (*s == NULL) {
    return -ENOMEM;
  }

  return 0;
}

static int copy_to_double(yajl_val node, double *d) {
  *d = YAJL_GET_DOUBLE(node);

  return 0;
}

static int copy_to_integer(yajl_val node, int *i) {
  *i = YAJL_GET_INTEGER(node);

  return 0;
}

static int copy_to_array(yajl_val node, char ***l) {
  char **t;
  size_t i;

  if (YAJL_GET_ARRAY(node)->len == 0) {
    return 0;
  }

  t = calloc(YAJL_GET_ARRAY(node)->len + 1, sizeof(char*));
  if (t == NULL) {
    return -ENOMEM;
  }

  for (i = 0; i < YAJL_GET_ARRAY(node)->len; ++i) {
    int r;

    r = copy_to_string(YAJL_GET_ARRAY(node)->values[i], &t[i]);
    if (r < 0) {
      return r;
    }
  }

  *l = t;
  return 0;
}

static int copy_to_object(yajl_val node, const struct json_descriptor_t table[], size_t tabsize, uint8_t *output_base) {
  size_t i;

  for (i = 0; i < YAJL_GET_OBJECT(node)->len; ++i) {
    const char *k = YAJL_GET_OBJECT(node)->keys[i];
    yajl_val v = YAJL_GET_OBJECT(node)->values[i];
    void *dest;
    int r = 0;

    const struct json_descriptor_t *json_desc = descmap_get_key(table, tabsize, k);
    if (json_desc == NULL) {
      fprintf(stderr, "error: lookup failed for key=%s\n", k);
      continue;
    }

    /* don't handle this, just leave the field empty */
    if (v->type == yajl_t_null) {
      continue;
    }

    if (v->type != json_desc->type) {
      fprintf(stderr, "error: type mismatch for key=%s: got=%d, expected=%d\n", k, v->type, json_desc->type);
      continue;
    }

    dest = output_base + json_desc->offset;
    if (YAJL_IS_STRING(v)) {
      r = copy_to_string(v, dest);
    } else if (YAJL_IS_INTEGER(v)) {
      r = copy_to_integer(v, dest);
    } else if (YAJL_IS_DOUBLE(v)) {
      r = copy_to_double(v, dest);
    } else if (YAJL_IS_ARRAY(v)) {
      r = copy_to_array(v, dest);
    } else {
      fprintf(stderr, "warning: unhandled type %d for key %s\n", v->type, k);
    }

    if (r < 0) {
      return r;
    }
  }

  return 0;
}

int aur_packages_from_json(const char *json, aurpkg_t ***packages, int *count) {
  yajl_val node, results;
  char error_buffer[1024];
  const char *results_path[] = { "results", NULL };
  _cleanup_packages_free_ aurpkg_t **p = NULL;
  size_t i;

  static const struct json_descriptor_t table[] = {
    {"CategoryID",     yajl_t_number, offsetof(aurpkg_t, category_id) },
    {"CheckDepends",   yajl_t_array,  offsetof(aurpkg_t, checkdepends) },
    {"Conflicts",      yajl_t_array,  offsetof(aurpkg_t, conflicts) },
    {"Depends",        yajl_t_array,  offsetof(aurpkg_t, depends) },
    {"Description",    yajl_t_string, offsetof(aurpkg_t, description) },
    {"FirstSubmitted", yajl_t_number, offsetof(aurpkg_t, submitted_s) },
    {"Groups",         yajl_t_array,  offsetof(aurpkg_t, groups) },
    {"ID",             yajl_t_number, offsetof(aurpkg_t, package_id) },
    {"Keywords",       yajl_t_array,  offsetof(aurpkg_t, keywords) },
    {"LastModified",   yajl_t_number, offsetof(aurpkg_t, modified_s) },
    {"License",        yajl_t_array,  offsetof(aurpkg_t, licenses) },
    {"Maintainer",     yajl_t_string, offsetof(aurpkg_t, maintainer) },
    {"MakeDepends",    yajl_t_array,  offsetof(aurpkg_t, makedepends) },
    {"Name",           yajl_t_string, offsetof(aurpkg_t, name) },
    {"NumVotes",       yajl_t_number, offsetof(aurpkg_t, votes) },
    {"OptDepends",     yajl_t_array,  offsetof(aurpkg_t, optdepends) },
    {"OutOfDate",      yajl_t_number, offsetof(aurpkg_t, out_of_date) },
    {"PackageBase",    yajl_t_string, offsetof(aurpkg_t, pkgbase) },
    {"PackageBaseID",  yajl_t_number, offsetof(aurpkg_t, pkgbaseid) },
    {"Popularity",     yajl_t_number, offsetof(aurpkg_t, popularity) },
    {"Provides",       yajl_t_array,  offsetof(aurpkg_t, provides) },
    {"Replaces",       yajl_t_array,  offsetof(aurpkg_t, replaces) },
    {"URL",            yajl_t_string, offsetof(aurpkg_t, upstream_url) },
    {"URLPath",        yajl_t_string, offsetof(aurpkg_t, aur_urlpath) },
    {"Version",        yajl_t_string, offsetof(aurpkg_t, version) },
  };

  node = yajl_tree_parse(json, error_buffer, sizeof(error_buffer));
  if (node == NULL) {
    return -EINVAL;
  }

  results = yajl_tree_get(node, results_path, yajl_t_array);
  if (!YAJL_IS_ARRAY(results)) {
    return -EBADMSG;
  }

  if (YAJL_GET_ARRAY(results)->len > 0) {
    p = calloc(YAJL_GET_ARRAY(results)->len + 1, sizeof(*p));
    if (p == NULL) {
      return -ENOMEM;
    }

    for (i = 0; i < YAJL_GET_ARRAY(results)->len; ++i) {
      int r;

      p[i] = calloc(1, sizeof(*p[i]));
      if (p[i] == NULL) {
        return -ENOMEM;
      }

      r = copy_to_object(YAJL_GET_ARRAY(results)->values[i], table, ARRAYSIZE(table), (uint8_t*)p[i]);
      if (r < 0) {
        return r;
      }
    }
  }

  *count = YAJL_GET_ARRAY(results)->len;
  *packages = p;
  p = NULL;

  yajl_tree_free(node);

  return 0;
}

int aur_packages_count(aurpkg_t **l) {
  aurpkg_t **p;
  int count = 0;

  for (p = l; *p; p++) {
    ++count;
  }

  return count;
}

int aur_packages_append(aurpkg_t ***dest, aurpkg_t **src) {
  int dcount, scount;
  aurpkg_t **out;

  if (*dest == NULL) {
    *dest = src;
    return 0;
  }

  dcount = aur_packages_count(*dest);
  scount = aur_packages_count(src);

  out = realloc(*dest, (dcount + scount + 1) * sizeof(*dest));
  if (out == NULL) {
    return -ENOMEM;
  }

  memcpy(&out[dcount], src, scount * sizeof(*src));
  free(src);

  out[dcount + scount] = NULL;
  *dest = out;

  return 0;
}
