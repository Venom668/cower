/*
 *  depends.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "alpmutil.h"
#include "conf.h"
#include "depends.h"
#include "download.h"
#include "search.h"
#include "util.h"

alpm_list_t *parse_bash_array(alpm_list_t *deplist, char *startdep) {

  char *token;

  while ((token = strsep(&startdep, " ")) != NULL) {
    if (strlen(token) <= 1) continue; /* Kludgy */
    if (*token == '\'' || *token == '\"')
      token++;
    char *i;
    for (i = token; *i != '\0'; i++) {
      switch (*i) {
      case '=':
      case '<':
      case '>':
      case '\'':
        *i = '\0';
        break;
      }
    }
    if (config->verbose >= 2) {
      fprintf(stderr, "::DEBUG Adding Depend: %s\n", token);
    }
    deplist = alpm_list_add(deplist, strdup(token));
  }

  return deplist;

}

alpm_list_t *pkgbuild_get_deps(const char *pkgbuild, alpm_list_t *deplist) {
  FILE* fd;
  char buffer[BUFSIZ + 1];
  char *deps, *tmp, *bptr;

  fd = fopen(pkgbuild, "r");
  fread(buffer, sizeof(char), BUFSIZ, fd); 

  bptr = buffer;

  /* This catches depends as well as makedepends.
   * It's valid for multi package files as well,
   * even though the AUR doesn't support them. */
  while ((deps = strstr(bptr, "depends=(")) != NULL) {
    tmp = strndup(deps + 9, strchr(deps, ')') - deps - 9);
    bptr = deps + 10;
    deplist = parse_bash_array(deplist, tmp);
    free(tmp);
  }

  fclose(fd);

  return deplist;
}

int get_pkg_dependencies(const char *pkg) {

  const char *dir;
  char *pkgbuild_path;

  if (config->download_dir == NULL)
    dir = getcwd(NULL, 0);
  else
    dir = realpath(config->download_dir, NULL);

  pkgbuild_path = calloc(1, PATH_MAX + 1);
  snprintf(pkgbuild_path, strlen(dir) + strlen(pkg) + 11, "%s/%s/PKGBUILD", dir, pkg);

  alpm_list_t *deplist = NULL;
  deplist = pkgbuild_get_deps(pkgbuild_path, deplist);

  if (! config->quiet) {
    if (config->color) {
      cprintf("\n%<::%> Fetching uninstalled dependencies for %<%s%>...\n",
        BLUE, WHITE, pkg);
    } else {
      printf("\n:: Fetching uninstalled dependencies for %s...\n", pkg);
    }
  }

  alpm_list_t *i = deplist;
  for (i = deplist; i; i = alpm_list_next(i)) {
    const char* depend = i->data;
    if (config->verbose >= 2)
      printf("::DEBUG Attempting to find %s\n", depend);

    if (alpm_db_get_pkg(db_local, depend)) { /* installed */
      if (config->verbose >= 2)
        printf("::DEBUG %s is installed\n", depend);
      continue;
    }
    if (is_in_pacman(depend)) { /* available in pacman */
      continue;
    }
    /* if we're here, we need to check the AUR */
    json_t *infojson = aur_rpc_query(AUR_QUERY_TYPE_INFO, depend);
    if (infojson) {
      if (config->verbose >= 2)
        printf("::DEBUG %s is in the AUR\n", depend);
      aur_get_tarball(infojson);
    }
    json_decref(infojson);

  }

  /* Cleanup */
  alpm_list_free_inner(deplist, free);
  alpm_list_free(deplist);
  FREE(dir);
  FREE(pkgbuild_path);

  return 0;
}
