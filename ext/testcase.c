/*
 * Copyright (c) 2012, Novell Inc.
 *
 * This program is licensed under the BSD license, read LICENSE.BSD
 * for further information
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pool.h"
#include "poolarch.h"
#include "poolvendor.h"
#include "evr.h"
#include "repo.h"
#include "repo_solv.h"
#include "solver.h"
#include "solverdebug.h"
#include "chksum.h"
#include "testcase.h"
#include "selection.h"
#include "solv_xfopen.h"
#if ENABLE_TESTCASE_HELIXREPO
#include "ext/repo_helix.h"
#endif

#ifdef _WIN32
  #include <direct.h>
#endif

/* see repo_testcase.c */
struct oplist {
  Id flags;
  const char *opname;
};
extern struct oplist oplist[];


static struct job2str {
  Id job;
  const char *str;
} job2str[] = {
  { SOLVER_NOOP,           "noop" },
  { SOLVER_INSTALL,        "install" },
  { SOLVER_ERASE,          "erase" },
  { SOLVER_UPDATE,         "update" },
  { SOLVER_WEAKENDEPS,     "weakendeps" },
  { SOLVER_MULTIVERSION,   "multiversion" },
  { SOLVER_MULTIVERSION,   "noobsoletes" },	/* old name */
  { SOLVER_LOCK,           "lock" },
  { SOLVER_DISTUPGRADE,    "distupgrade" },
  { SOLVER_VERIFY,         "verify" },
  { SOLVER_DROP_ORPHANED,  "droporphaned" },
  { SOLVER_USERINSTALLED,  "userinstalled" },
  { SOLVER_ALLOWUNINSTALL, "allowuninstall" },
  { SOLVER_FAVOR,          "favor" },
  { SOLVER_DISFAVOR,       "disfavor" },
  { SOLVER_BLACKLIST,      "blacklist" },
  { SOLVER_EXCLUDEFROMWEAK,   "excludefromweak" },
  { 0, 0 }
};

static struct jobflags2str {
  Id flag;
  const char *str;
} jobflags2str[] = {
  { SOLVER_WEAK,      "weak" },
  { SOLVER_ESSENTIAL, "essential" },
  { SOLVER_CLEANDEPS, "cleandeps" },
  { SOLVER_ORUPDATE,  "orupdate" },
  { SOLVER_FORCEBEST, "forcebest" },
  { SOLVER_TARGETED,  "targeted" },
  { SOLVER_NOTBYUSER, "notbyuser" },
  { SOLVER_SETEV,     "setev" },
  { SOLVER_SETEVR,    "setevr" },
  { SOLVER_SETARCH,   "setarch" },
  { SOLVER_SETVENDOR, "setvendor" },
  { SOLVER_SETREPO,   "setrepo" },
  { SOLVER_NOAUTOSET, "noautoset" },
  { 0, 0 }
};

static struct resultflags2str {
  Id flag;
  const char *str;
} resultflags2str[] = {
  { TESTCASE_RESULT_TRANSACTION,	"transaction" },
  { TESTCASE_RESULT_PROBLEMS,		"problems" },
  { TESTCASE_RESULT_ORPHANED,		"orphaned" },
  { TESTCASE_RESULT_RECOMMENDED,	"recommended" },
  { TESTCASE_RESULT_UNNEEDED,		"unneeded" },
  { TESTCASE_RESULT_ALTERNATIVES,	"alternatives" },
  { TESTCASE_RESULT_RULES,		"rules" },
  { TESTCASE_RESULT_GENID,		"genid" },
  { TESTCASE_RESULT_REASON,		"reason" },
  { TESTCASE_RESULT_CLEANDEPS,		"cleandeps" },
  { TESTCASE_RESULT_JOBS,		"jobs" },
  { TESTCASE_RESULT_USERINSTALLED,	"userinstalled" },
  { TESTCASE_RESULT_ORDER,		"order" },
  { TESTCASE_RESULT_ORDEREDGES,		"orderedges" },
  { TESTCASE_RESULT_PROOF,		"proof" },
  { 0, 0 }
};

static struct solverflags2str {
  Id flag;
  const char *str;
  int def;
} solverflags2str[] = {
  { SOLVER_FLAG_ALLOW_DOWNGRADE,            "allowdowngrade", 0 },
  { SOLVER_FLAG_ALLOW_NAMECHANGE,           "allownamechange", 1 },
  { SOLVER_FLAG_ALLOW_ARCHCHANGE,           "allowarchchange", 0 },
  { SOLVER_FLAG_ALLOW_VENDORCHANGE,         "allowvendorchange", 0 },
  { SOLVER_FLAG_ALLOW_UNINSTALL,            "allowuninstall", 0 },
  { SOLVER_FLAG_NO_UPDATEPROVIDE,           "noupdateprovide", 0 },
  { SOLVER_FLAG_SPLITPROVIDES,              "splitprovides", 0 },
  { SOLVER_FLAG_IGNORE_RECOMMENDED,         "ignorerecommended", 0 },
  { SOLVER_FLAG_ADD_ALREADY_RECOMMENDED,    "addalreadyrecommended", 0 },
  { SOLVER_FLAG_NO_INFARCHCHECK,            "noinfarchcheck", 0 },
  { SOLVER_FLAG_KEEP_EXPLICIT_OBSOLETES,    "keepexplicitobsoletes", 0 },
  { SOLVER_FLAG_BEST_OBEY_POLICY,           "bestobeypolicy", 0 },
  { SOLVER_FLAG_NO_AUTOTARGET,              "noautotarget", 0 },
  { SOLVER_FLAG_DUP_ALLOW_DOWNGRADE,        "dupallowdowngrade", 1 },
  { SOLVER_FLAG_DUP_ALLOW_ARCHCHANGE,       "dupallowarchchange", 1 },
  { SOLVER_FLAG_DUP_ALLOW_VENDORCHANGE,     "dupallowvendorchange", 1 },
  { SOLVER_FLAG_DUP_ALLOW_NAMECHANGE,       "dupallownamechange", 1 },
  { SOLVER_FLAG_KEEP_ORPHANS,               "keeporphans", 0 },
  { SOLVER_FLAG_BREAK_ORPHANS,              "breakorphans", 0 },
  { SOLVER_FLAG_FOCUS_INSTALLED,            "focusinstalled", 0 },
  { SOLVER_FLAG_YUM_OBSOLETES,              "yumobsoletes", 0 },
  { SOLVER_FLAG_NEED_UPDATEPROVIDE,         "needupdateprovide", 0 },
  { SOLVER_FLAG_URPM_REORDER,               "urpmreorder", 0 },
  { SOLVER_FLAG_FOCUS_BEST,                 "focusbest", 0 },
  { SOLVER_FLAG_STRONG_RECOMMENDS,          "strongrecommends", 0 },
  { SOLVER_FLAG_INSTALL_ALSO_UPDATES,       "installalsoupdates", 0 },
  { SOLVER_FLAG_ONLY_NAMESPACE_RECOMMENDED, "onlynamespacerecommended", 0 },
  { SOLVER_FLAG_STRICT_REPO_PRIORITY,       "strictrepopriority", 0 },
  { SOLVER_FLAG_FOCUS_NEW,                  "focusnew", 0 },
  { 0, 0, 0 }
};

static struct poolflags2str {
  Id flag;
  const char *str;
  int def;
} poolflags2str[] = {
  { POOL_FLAG_PROMOTEEPOCH,                 "promoteepoch", 0 },
  { POOL_FLAG_FORBIDSELFCONFLICTS,          "forbidselfconflicts", 0 },
  { POOL_FLAG_OBSOLETEUSESPROVIDES,         "obsoleteusesprovides", 0 },
  { POOL_FLAG_IMPLICITOBSOLETEUSESPROVIDES, "implicitobsoleteusesprovides", 0 },
  { POOL_FLAG_OBSOLETEUSESCOLORS,           "obsoleteusescolors", 0 },
  { POOL_FLAG_IMPLICITOBSOLETEUSESCOLORS,   "implicitobsoleteusescolors", 0 },
  { POOL_FLAG_NOINSTALLEDOBSOLETES,         "noinstalledobsoletes", 0 },
  { POOL_FLAG_HAVEDISTEPOCH,                "havedistepoch", 0 },
  { POOL_FLAG_NOOBSOLETESMULTIVERSION,      "noobsoletesmultiversion", 0 },
  { POOL_FLAG_ADDFILEPROVIDESFILTERED,      "addfileprovidesfiltered", 0 },
  { POOL_FLAG_NOWHATPROVIDESAUX,            "nowhatprovidesaux", 0 },
  { POOL_FLAG_WHATPROVIDESWITHDISABLED,     "whatprovideswithdisabled", 0 },
  { 0, 0, 0 }
};

static struct disttype2str {
  Id type;
  const char *str;
} disttype2str[] = {
  { DISTTYPE_RPM,  "rpm" },
  { DISTTYPE_DEB,  "deb" },
  { DISTTYPE_ARCH, "arch" },
  { DISTTYPE_HAIKU, "haiku" },
  { DISTTYPE_CONDA, "conda" },
  { DISTTYPE_APK, "apk" },
  { 0, 0 }
};

static struct selflags2str {
  Id flag;
  const char *str;
} selflags2str[] = {
  { SELECTION_NAME, "name" },
  { SELECTION_PROVIDES, "provides" },
  { SELECTION_FILELIST, "filelist" },
  { SELECTION_CANON, "canon" },
  { SELECTION_DOTARCH, "dotarch" },
  { SELECTION_REL, "rel" },
  { SELECTION_INSTALLED_ONLY, "installedonly" },
  { SELECTION_GLOB, "glob" },
  { SELECTION_FLAT, "flat" },
  { SELECTION_NOCASE, "nocase" },
  { SELECTION_SOURCE_ONLY, "sourceonly" },
  { SELECTION_WITH_SOURCE, "withsource" },
  { SELECTION_SKIP_KIND, "skipkind" },
  { SELECTION_MATCH_DEPSTR, "depstr" },
  { SELECTION_WITH_DISABLED, "withdisabled" },
  { SELECTION_WITH_BADARCH, "withbadarch" },
  { SELECTION_ADD, "add" },
  { SELECTION_SUBTRACT, "subtract" },
  { SELECTION_FILTER, "filter" },
  { 0, 0 }
};

static const char *features[] = {
#ifdef ENABLE_LINKED_PKGS
  "linked_packages",
#endif
#ifdef ENABLE_COMPLEX_DEPS
  "complex_deps",
#endif
#if ENABLE_TESTCASE_HELIXREPO
  "testcase_helixrepo",
#endif
  0
};

typedef struct strqueue {
  char **str;
  int nstr;
} Strqueue;

#define STRQUEUE_BLOCK 63

static void
strqueue_init(Strqueue *q)
{
  q->str = 0;
  q->nstr = 0;
}

static void
strqueue_free(Strqueue *q)
{
  int i;
  for (i = 0; i < q->nstr; i++)
    solv_free(q->str[i]);
  q->str = solv_free(q->str);
  q->nstr = 0;
}

static void
strqueue_push(Strqueue *q, const char *s)
{
  q->str = solv_extend(q->str, q->nstr, 1, sizeof(*q->str), STRQUEUE_BLOCK);
  q->str[q->nstr++] = solv_strdup(s);
}

static void
strqueue_pushjoin(Strqueue *q, const char *s1, const char *s2, const char *s3)
{
  q->str = solv_extend(q->str, q->nstr, 1, sizeof(*q->str), STRQUEUE_BLOCK);
  q->str[q->nstr++] = solv_dupjoin(s1, s2, s3);
}

static int
strqueue_sort_cmp(const void *ap, const void *bp, void *dp)
{
  const char *a = *(const char **)ap;
  const char *b = *(const char **)bp;
  return strcmp(a ? a : "", b ? b : "");
}

static void
strqueue_sort(Strqueue *q)
{
  if (q->nstr > 1)
    solv_sort(q->str, q->nstr, sizeof(*q->str), strqueue_sort_cmp, 0);
}

static void
strqueue_sort_u(Strqueue *q)
{
  int i, j;
  strqueue_sort(q);
  for (i = j = 0; i < q->nstr; i++)
    if (!j || strqueue_sort_cmp(q->str + i, q->str + j - 1, 0) != 0)
      q->str[j++] = q->str[i];
  q->nstr = j;
}

static char *
strqueue_join(Strqueue *q)
{
  int i, l = 0;
  char *r, *rp;
  for (i = 0; i < q->nstr; i++)
    if (q->str[i])
      l += strlen(q->str[i]) + 1;
  l++;	/* trailing \0 */
  r = solv_malloc(l);
  rp = r;
  for (i = 0; i < q->nstr; i++)
    if (q->str[i])
      {
        strcpy(rp, q->str[i]);
        rp += strlen(rp);
	*rp++ = '\n';
      }
  *rp = 0;
  return r;
}

static void
strqueue_split(Strqueue *q, const char *s)
{
  const char *p;
  if (!s)
    return;
  while ((p = strchr(s, '\n')) != 0)
    {
      q->str = solv_extend(q->str, q->nstr, 1, sizeof(*q->str), STRQUEUE_BLOCK);
      q->str[q->nstr] = solv_malloc(p - s + 1);
      if (p > s)
	memcpy(q->str[q->nstr], s, p - s);
      q->str[q->nstr][p - s] = 0;
      q->nstr++;
      s = p + 1;
    }
  if (*s)
    strqueue_push(q, s);
}

static void
strqueue_diff(Strqueue *sq1, Strqueue *sq2, Strqueue *osq)
{
  int i = 0, j = 0;
  while (i < sq1->nstr && j < sq2->nstr)
    {
      int r = strqueue_sort_cmp(sq1->str + i, sq2->str + j, 0);
      if (!r)
	i++, j++;
      else if (r < 0)
	strqueue_pushjoin(osq, "-", sq1->str[i++], 0);
      else
	strqueue_pushjoin(osq, "+", sq2->str[j++], 0);
    }
  while (i < sq1->nstr)
    strqueue_pushjoin(osq, "-", sq1->str[i++], 0);
  while (j < sq2->nstr)
    strqueue_pushjoin(osq, "+", sq2->str[j++], 0);
}

/**********************************************************/

const char *
testcase_repoid2str(Pool *pool, Id repoid)
{
  Repo *repo = pool_id2repo(pool, repoid);
  if (repo->name)
    {
      char *r = pool_tmpjoin(pool, repo->name, 0, 0);
      char *rp;
      for (rp = r; *rp; rp++)
	if (*rp == ' ' || *rp == '\t')
	  *rp = '_';
      return r;
    }
  else
    {
      char buf[20];
      sprintf(buf, "#%d", repoid);
      return pool_tmpjoin(pool, buf, 0, 0);
    }
}

const char *
testcase_solvid2str(Pool *pool, Id p)
{
  Solvable *s = pool->solvables + p;
  const char *n, *e, *a;
  char *str, buf[20];

  if (p == SYSTEMSOLVABLE)
    return "@SYSTEM";
  n = pool_id2str(pool, s->name);
  e = pool_id2str(pool, s->evr);
  a = pool_id2str(pool, s->arch);
  str = pool_alloctmpspace(pool, strlen(n) + strlen(e) + strlen(a) + 3);
  sprintf(str, "%s-%s", n, e);
  if (solvable_lookup_type(s, SOLVABLE_BUILDFLAVOR))
    {
      Queue flavorq;
      int i;

      queue_init(&flavorq);
      solvable_lookup_idarray(s, SOLVABLE_BUILDFLAVOR, &flavorq);
      for (i = 0; i < flavorq.count; i++)
	str = pool_tmpappend(pool, str, "-", pool_id2str(pool, flavorq.elements[i]));
      queue_free(&flavorq);
    }
  if (s->arch)
    str = pool_tmpappend(pool, str, ".", a);
  if (!s->repo)
    return pool_tmpappend(pool, str, "@", 0);
  if (s->repo->name)
    {
      int l = strlen(str);
      str = pool_tmpappend(pool, str, "@", s->repo->name);
      for (; str[l]; l++)
	if (str[l] == ' ' || str[l] == '\t')
	  str[l] = '_';
      return str;
    }
  sprintf(buf, "@#%d", s->repo->repoid);
  return pool_tmpappend(pool, str, buf, 0);
}

Repo *
testcase_str2repo(Pool *pool, const char *str)
{
  int repoid;
  Repo *repo = 0;
  if (str[0] == '#' && (str[1] >= '0' && str[1] <= '9'))
    {
      int j;
      repoid = 0;
      for (j = 1; str[j] >= '0' && str[j] <= '9'; j++)
	repoid = repoid * 10 + (str[j] - '0');
      if (!str[j] && repoid > 0 && repoid < pool->nrepos)
	repo = pool_id2repo(pool, repoid);
    }
  if (!repo)
    {
      FOR_REPOS(repoid, repo)
	{
	  int i, l;
	  if (!repo->name)
	    continue;
	  l = strlen(repo->name);
	  for (i = 0; i < l; i++)
	    {
	      int c = repo->name[i];
	      if (c == ' ' || c == '\t')
		c = '_';
	      if (c != str[i])
		break;
	    }
	  if (i == l && !str[l])
	    break;
	}
      if (repoid >= pool->nrepos)
	repo = 0;
    }
  return repo;
}

static const char *
testcase_escape(Pool *pool, const char *str)
{
  size_t nbad = 0;
  const char *p;
  char *new, *np;
  for (p = str; *p; p++)
    if (*p == '\\' || *p == ' ' || *p == '\t')
      nbad++;
  if (!nbad)
    return str;
  new = pool_alloctmpspace(pool, strlen(str) + 1 + nbad * 2);
  for (np = new, p = str; *p; p++)
    {
      *np++ = *p;
      if (*p == '\\' || *p == ' ' || *p == '\t')
	{
	  np[-1] = '\\';
	  solv_bin2hex((unsigned char *)p, 1, np);
	  np += 2;
	}
    }
  *np = 0;
  return new;
}

static void
testcase_unescape_inplace(char *str)
{
  char *p, *q;
  for (p = q = str; *p;)
    {
      *q++ = *p++;
      if (p[-1] == '\\')
	solv_hex2bin((const char **)&p, (unsigned char *)q - 1, 1);
    }
  *q = 0;
}

/* check evr and buildflavors */
static int
str2solvid_check(Pool *pool, Solvable *s, const char *start, const char *end, Id evrid)
{
  if (!solvable_lookup_type(s, SOLVABLE_BUILDFLAVOR))
    {
      /* just check the evr */
      return evrid && s->evr == evrid;
    }
  else
    {
      Queue flavorq;
      int i;

      queue_init(&flavorq);
      solvable_lookup_idarray(s, SOLVABLE_BUILDFLAVOR, &flavorq);
      queue_unshift(&flavorq, s->evr);
      for (i = 0; i < flavorq.count; i++)
	{
	  const char *part = pool_id2str(pool, flavorq.elements[i]);
	  size_t partl = strlen(part);
	  if (start + partl > end || strncmp(start, part, partl) != 0)
	    break;
	  start += partl;
	  if (i + 1 < flavorq.count)
	    {
	      if (start >= end || *start != '-')
		break;
	      start++;
	    }
	}
      if (i < flavorq.count)
	{
	  queue_free(&flavorq);
	  return 0;
	}
      queue_free(&flavorq);
      return start == end;
    }
}

Id
testcase_str2solvid(Pool *pool, const char *str)
{
  int i, l = strlen(str);
  int repostart;
  Repo *repo;
  Id arch;

  if (!l)
    return 0;
  if (*str == '@' && !strcmp(str, "@SYSTEM"))
    return SYSTEMSOLVABLE;
  repo = 0;
  for (i = l - 1; i >= 0; i--)
    if (str[i] == '@' && (repo = testcase_str2repo(pool, str + i + 1)) != 0)
      break;
  if (i < 0)
    i = l;
  repostart = i;
  /* now find the arch (if present) */
  arch = 0;
  for (i = repostart - 1; i > 0; i--)
    if (str[i] == '.')
      {
	arch = pool_strn2id(pool, str + i + 1, repostart - (i + 1), 0);
	if (arch)
	  repostart = i;
	break;
      }
  /* now find the name */
  for (i = repostart - 1; i > 0; i--)
    {
      if (str[i] == '-')
	{
	  Id nid, evrid, p, pp;
	  nid = pool_strn2id(pool, str, i, 0);
	  if (!nid)
	    continue;
	  evrid = pool_strn2id(pool, str + i + 1, repostart - (i + 1), 0);
	  /* first check whatprovides */
	  FOR_PROVIDES(p, pp, nid)
	    {
	      Solvable *s = pool->solvables + p;
	      if (s->name != nid)
		continue;
	      if (repo && s->repo != repo)
		continue;
	      if (arch && s->arch != arch)
		continue;
	      if (str2solvid_check(pool, s, str + i + 1, str + repostart, evrid))
	        return p;
	    }
	  /* maybe it's not installable and thus not in whatprovides. do a slow search */
	  if (repo)
	    {
	      Solvable *s;
	      FOR_REPO_SOLVABLES(repo, p, s)
		{
		  if (s->name != nid)
		    continue;
		  if (arch && s->arch != arch)
		    continue;
		  if (str2solvid_check(pool, s, str + i + 1, str + repostart, evrid))
		    return p;
		}
	    }
	  else
	    {
	      FOR_POOL_SOLVABLES(p)
		{
		  Solvable *s = pool->solvables + p;
		  if (s->name != nid)
		    continue;
		  if (arch && s->arch != arch)
		    continue;
		  if (str2solvid_check(pool, s, str + i + 1, str + repostart, evrid))
		    return p;
		}
	    }
	}
    }
  return 0;
}

const char *
testcase_job2str(Pool *pool, Id how, Id what)
{
  char *ret;
  const char *jobstr;
  const char *selstr;
  const char *pkgstr;
  int i, o;
  Id select = how & SOLVER_SELECTMASK;

  for (i = 0; job2str[i].str; i++)
    if ((how & SOLVER_JOBMASK) == job2str[i].job)
      break;
  jobstr = job2str[i].str ? job2str[i].str : "unknown";
  if (select == SOLVER_SOLVABLE)
    {
      selstr = " pkg ";
      pkgstr = testcase_solvid2str(pool, what);
    }
  else if (select == SOLVER_SOLVABLE_NAME)
    {
      selstr = " name ";
      pkgstr = testcase_dep2str(pool, what);
    }
  else if (select == SOLVER_SOLVABLE_PROVIDES)
    {
      selstr = " provides ";
      pkgstr = testcase_dep2str(pool, what);
    }
  else if (select == SOLVER_SOLVABLE_ONE_OF)
    {
      Id p;
      selstr = " oneof ";
      pkgstr = 0;
      while ((p = pool->whatprovidesdata[what++]) != 0)
	{
	  const char *s = testcase_solvid2str(pool, p);
	  if (pkgstr)
	    {
	      pkgstr = pool_tmpappend(pool, pkgstr, " ", s);
	      pool_freetmpspace(pool, s);
	    }
	  else
	    pkgstr = s;
	}
      if (!pkgstr)
	pkgstr = "nothing";
    }
  else if (select == SOLVER_SOLVABLE_REPO)
    {
      Repo *repo = pool_id2repo(pool, what);
      selstr = " repo ";
      if (!repo->name)
	{
          char buf[20];
	  sprintf(buf, "#%d", repo->repoid);
	  pkgstr = pool_tmpjoin(pool, buf, 0, 0);
	}
      else
        pkgstr = pool_tmpjoin(pool, repo->name, 0, 0);
    }
  else if (select == SOLVER_SOLVABLE_ALL)
    {
      selstr = " all ";
      pkgstr = "packages";
    }
  else
    {
      selstr = " unknown ";
      pkgstr = "unknown";
    }
  ret = pool_tmpjoin(pool, jobstr, selstr, pkgstr);
  o = strlen(ret);
  ret = pool_tmpappend(pool, ret, " ", 0);
  for (i = 0; jobflags2str[i].str; i++)
    if ((how & jobflags2str[i].flag) != 0)
      ret = pool_tmpappend(pool, ret, ",", jobflags2str[i].str);
  if (!ret[o + 1])
    ret[o] = 0;
  else
    {
      ret[o + 1] = '[';
      ret = pool_tmpappend(pool, ret, "]", 0);
    }
  return ret;
}

static int
str2selflags(Pool *pool, char *s)	/* modifies the string! */
{
  int i, selflags = 0;
  while (s)
    {
      char *se = strchr(s, ',');
      if (se)
	*se++ = 0;
      for (i = 0; selflags2str[i].str; i++)
	if (!strcmp(s, selflags2str[i].str))
	  {
	    selflags |= selflags2str[i].flag;
	    break;
	  }
      if (!selflags2str[i].str)
	pool_error(pool, 0, "str2job: unknown selection flag '%s'", s);
      s = se;
    }
  return selflags;
}

static int
str2jobflags(Pool *pool, char *s)	/* modifies the string */
{
  int i, jobflags = 0;
  while (s)
    {
      char *se = strchr(s, ',');
      if (se)
	*se++ = 0;
      for (i = 0; jobflags2str[i].str; i++)
	if (!strcmp(s, jobflags2str[i].str))
	  {
	    jobflags |= jobflags2str[i].flag;
	    break;
	  }
      if (!jobflags2str[i].str)
	pool_error(pool, 0, "str2job: unknown job flag '%s'", s);
      s = se;
    }
  return jobflags;
}

static Id
testcase_str2jobsel(Pool *pool, const char *caller, char **pieces, int npieces, Id *whatp)
{
  Id job, what;
  if (!strcmp(pieces[0], "pkg") && npieces == 2)
    {
      job = SOLVER_SOLVABLE;
      what = testcase_str2solvid(pool, pieces[1]);
      if (!what)
	return pool_error(pool, -1, "%s: unknown package '%s'", caller, pieces[1]);
    }
  else if (!strcmp(pieces[0], "name") || !strcmp(pieces[0], "provides"))
    {
      /* join em again for dep2str... */
      char *sp;
      for (sp = pieces[1]; sp < pieces[npieces - 1]; sp++)
	if (*sp == 0)
	  *sp = ' ';
      what = 0;
      if (pieces[0][0] == 'p' && strncmp(pieces[1], "namespace:", 10) == 0)
	{
	  char *spe = strchr(pieces[1], '(');
	  int l = strlen(pieces[1]);
	  if (spe && pieces[1][l - 1] == ')')
	    {
	      /* special namespace provides */
	      if (strcmp(spe, "(<NULL>)") != 0)
		{
		  pieces[1][l - 1] = 0;
		  what = testcase_str2dep(pool, spe + 1);
		  pieces[1][l - 1] = ')';
		}
	      what = pool_rel2id(pool, pool_strn2id(pool, pieces[1], spe - pieces[1], 1), what, REL_NAMESPACE, 1);
	    }
	}
      if (!what)
        what = testcase_str2dep(pool, pieces[1]);
      if (pieces[0][0] == 'n')
	job = SOLVER_SOLVABLE_NAME;
      else
	job = SOLVER_SOLVABLE_PROVIDES;
    }
  else if (!strcmp(pieces[0], "oneof"))
    {
      Queue q;
      job = SOLVER_SOLVABLE_ONE_OF;
      queue_init(&q);
      if (npieces > 1 && strcmp(pieces[1], "nothing") != 0)
	{
	  int i;
	  for (i = 1; i < npieces; i++)
	    {
	      Id p = testcase_str2solvid(pool, pieces[i]);
	      if (!p)
		{
		  queue_free(&q);
		  return pool_error(pool, -1, "%s: unknown package '%s'", caller, pieces[i]);
		}
	      queue_push(&q, p);
	    }
	}
      what = pool_queuetowhatprovides(pool, &q);
      queue_free(&q);
    }
  else if (!strcmp(pieces[0], "repo") && npieces == 2)
    {
      Repo *repo = testcase_str2repo(pool, pieces[1]);
      if (!repo)
	return pool_error(pool, -1, "%s: unknown repo '%s'", caller, pieces[1]);
      job = SOLVER_SOLVABLE_REPO;
      what = repo->repoid;
    }
  else if (!strcmp(pieces[0], "all") && npieces == 2 && !strcmp(pieces[1], "packages"))
    {
      job = SOLVER_SOLVABLE_ALL;
      what = 0;
    }
  else
    {
      /* join em again for the error message... */
      char *sp;
      for (sp = pieces[0]; sp < pieces[npieces - 1]; sp++)
	if (*sp == 0)
	  *sp = ' ';
      return pool_error(pool, -1, "%s: bad line '%s'", caller, pieces[0]);
    }
  *whatp = what;
  return job;
}

Id
testcase_str2job(Pool *pool, const char *str, Id *whatp)
{
  int i;
  Id job, jobsel;
  Id what;
  char *s;
  char **pieces = 0;
  int npieces = 0;

  *whatp = 0;
  /* so we can patch it */
  s = pool_tmpjoin(pool, str, 0, 0);
  /* split it in pieces */
  for (;;)
    {
      while (*s == ' ' || *s == '\t')
	s++;
      if (!*s)
	break;
      pieces = solv_extend(pieces, npieces, 1, sizeof(*pieces), 7);
      pieces[npieces++] = s;
      while (*s && *s != ' ' && *s != '\t')
	s++;
      if (*s)
	*s++ = 0;
    }
  if (npieces < 3)
    {
      pool_error(pool, -1, "str2job: bad line '%s'", str);
      solv_free(pieces);
      return -1;
    }

  for (i = 0; job2str[i].str; i++)
    if (!strcmp(pieces[0], job2str[i].str))
      break;
  if (!job2str[i].str)
    {
      pool_error(pool, -1, "str2job: unknown job '%s'", str);
      solv_free(pieces);
      return -1;
    }
  job = job2str[i].job;
  what = 0;
  if (npieces > 3)
    {
      char *flags = pieces[npieces - 1];
      if (*flags == '[' && flags[strlen(flags) - 1] == ']')
	{
	  npieces--;
	  flags++;
	  flags[strlen(flags) - 1] = 0;
	  job |= str2jobflags(pool, flags);
	}
    }
  jobsel = testcase_str2jobsel(pool, "str2job", pieces + 1, npieces - 1, &what);
  solv_free(pieces);
  if (jobsel == -1)
    return -1;
  *whatp = what;
  return job | jobsel;
}

#define SELECTIONJOB_MATCHDEPS		1
#define SELECTIONJOB_MATCHDEPID		2
#define SELECTIONJOB_MATCHSOLVABLE	3

static int
addselectionjob(Pool *pool, char **pieces, int npieces, Queue *jobqueue, int type, int keyname)
{
  Id job;
  int i, r = 0;
  int selflags;
  Queue sel;
  char *sp;

  for (i = 0; job2str[i].str; i++)
    if (!strcmp(pieces[0], job2str[i].str))
      break;
  if (!job2str[i].str)
    return pool_error(pool, -1, "selstr2job: unknown job '%s'", pieces[0]);
  job = job2str[i].job;
  if (npieces > 3)
    {
      char *flags = pieces[npieces - 1];
      if (*flags == '[' && flags[strlen(flags) - 1] == ']')
	{
	  npieces--;
	  flags++;
	  flags[strlen(flags) - 1] = 0;
	  job |= str2jobflags(pool, flags);
	}
    }
  if (npieces < 4)
    return pool_error(pool, -1, "selstr2job: no selection flags");
  selflags = str2selflags(pool, pieces[npieces - 1]);
  /* re-join pieces */
  for (sp = pieces[2]; sp < pieces[npieces - 2]; sp++)
    if (*sp == 0)
      *sp = ' ';
  queue_init(&sel);
  if (selflags & (SELECTION_ADD | SELECTION_SUBTRACT | SELECTION_FILTER))
    {
      for (i = 0; i < jobqueue->count; i += 2)
	queue_push2(&sel, jobqueue->elements[i] & (SOLVER_SELECTMASK | SOLVER_SETMASK), jobqueue->elements[i + 1]);
      queue_empty(jobqueue);
    }
  if (!type)
    r = selection_make(pool, &sel, pieces[2], selflags);
  else if (type == SELECTIONJOB_MATCHDEPS)
    r = selection_make_matchdeps(pool, &sel, pieces[2], selflags, keyname, 0);
  else if (type == SELECTIONJOB_MATCHDEPID)
    r = selection_make_matchdepid(pool, &sel, testcase_str2dep(pool, pieces[2]), selflags, keyname, 0);
  else if (type == SELECTIONJOB_MATCHSOLVABLE)
    r = selection_make_matchsolvable(pool, &sel, testcase_str2solvid(pool, pieces[2]), selflags, keyname, 0);
  for (i = 0; i < sel.count; i += 2)
    queue_push2(jobqueue, job | sel.elements[i], sel.elements[i + 1]);
  queue_free(&sel);
  return r;
}

const char *
testcase_getpoolflags(Pool *pool)
{
  const char *str = 0;
  int i, v;
  for (i = 0; poolflags2str[i].str; i++)
    {
      v = pool_get_flag(pool, poolflags2str[i].flag);
      if (v == poolflags2str[i].def)
	continue;
      str = pool_tmpappend(pool, str, v ? " " : " !", poolflags2str[i].str);
    }
  return str ? str + 1 : "";
}

int
testcase_setpoolflags(Pool *pool, const char *str)
{
  const char *p = str, *s;
  int i, v;
  for (;;)
    {
      while (*p == ' ' || *p == '\t' || *p == ',')
	p++;
      v = 1;
      if (*p == '!')
	{
	  p++;
	  v = 0;
	}
      if (!*p)
	break;
      s = p;
      while (*p && *p != ' ' && *p != '\t' && *p != ',')
	p++;
      for (i = 0; poolflags2str[i].str; i++)
	if (!strncmp(poolflags2str[i].str, s, p - s) && poolflags2str[i].str[p - s] == 0)
	  break;
      if (!poolflags2str[i].str)
        return pool_error(pool, 0, "setpoolflags: unknown flag '%.*s'", (int)(p - s), s);
      pool_set_flag(pool, poolflags2str[i].flag, v);
    }
  return 1;
}

void
testcase_resetpoolflags(Pool *pool)
{
  int i;
  for (i = 0; poolflags2str[i].str; i++)
    pool_set_flag(pool, poolflags2str[i].flag, poolflags2str[i].def);
}

const char *
testcase_getsolverflags(Solver *solv)
{
  Pool *pool = solv->pool;
  const char *str = 0;
  int i, v;
  for (i = 0; solverflags2str[i].str; i++)
    {
      v = solver_get_flag(solv, solverflags2str[i].flag);
      if (v == solverflags2str[i].def)
	continue;
      str = pool_tmpappend(pool, str, v ? " " : " !", solverflags2str[i].str);
    }
  return str ? str + 1 : "";
}

int
testcase_setsolverflags(Solver *solv, const char *str)
{
  const char *p = str, *s;
  int i, v;
  for (;;)
    {
      while (*p == ' ' || *p == '\t' || *p == ',')
	p++;
      v = 1;
      if (*p == '!')
	{
	  p++;
	  v = 0;
	}
      if (!*p)
	break;
      s = p;
      while (*p && *p != ' ' && *p != '\t' && *p != ',')
	p++;
      for (i = 0; solverflags2str[i].str; i++)
	if (!strncmp(solverflags2str[i].str, s, p - s) && solverflags2str[i].str[p - s] == 0)
	  break;
      if (!solverflags2str[i].str)
	return pool_error(solv->pool, 0, "setsolverflags: unknown flag '%.*s'", (int)(p - s), s);
      if (solver_set_flag(solv, solverflags2str[i].flag, v) == -1)
        return pool_error(solv->pool, 0, "setsolverflags: unsupported flag '%s'", solverflags2str[i].str);
    }
  return 1;
}

void
testcase_resetsolverflags(Solver *solv)
{
  int i;
  for (i = 0; solverflags2str[i].str; i++)
    solver_set_flag(solv, solverflags2str[i].flag, solverflags2str[i].def);
}

static const char *
testcase_ruleid(Solver *solv, Id rid)
{
  Strqueue sq;
  Queue q;
  int i;
  Chksum *chk;
  const unsigned char *md5;
  int md5l;
  const char *s;

  queue_init(&q);
  strqueue_init(&sq);
  solver_ruleliterals(solv, rid, &q);
  for (i = 0; i < q.count; i++)
    {
      Id p = q.elements[i];
      s = testcase_solvid2str(solv->pool, p > 0 ? p : -p);
      if (p < 0)
	s = pool_tmpjoin(solv->pool, "!", s, 0);
      strqueue_push(&sq, s);
    }
  queue_free(&q);
  strqueue_sort_u(&sq);
  chk = solv_chksum_create(REPOKEY_TYPE_MD5);
  for (i = 0; i < sq.nstr; i++)
    solv_chksum_add(chk, sq.str[i], strlen(sq.str[i]) + 1);
  md5 = solv_chksum_get(chk, &md5l);
  s = pool_bin2hex(solv->pool, md5, md5l);
  chk = solv_chksum_free(chk, 0);
  strqueue_free(&sq);
  return s;
}

static const char *
testcase_problemid(Solver *solv, Id problem)
{
  Strqueue sq;
  Queue q;
  Chksum *chk;
  const unsigned char *md5;
  int i, md5l;
  const char *s;

  /* we build a hash of all rules that define the problem */
  queue_init(&q);
  strqueue_init(&sq);
  solver_findallproblemrules(solv, problem, &q);
  for (i = 0; i < q.count; i++)
    strqueue_push(&sq, testcase_ruleid(solv, q.elements[i]));
  queue_free(&q);
  strqueue_sort_u(&sq);
  chk = solv_chksum_create(REPOKEY_TYPE_MD5);
  for (i = 0; i < sq.nstr; i++)
    solv_chksum_add(chk, sq.str[i], strlen(sq.str[i]) + 1);
  md5 = solv_chksum_get(chk, &md5l);
  s = pool_bin2hex(solv->pool, md5, 4);
  chk = solv_chksum_free(chk, 0);
  strqueue_free(&sq);
  return s;
}

static const char *
testcase_solutionid(Solver *solv, Id problem, Id solution)
{
  Id intid;
  Chksum *chk;
  const unsigned char *md5;
  int md5l;
  const char *s;

  intid = solver_solutionelement_internalid(solv, problem, solution);
  /* internal stuff! handle with care! */
  if (intid < 0)
    {
      /* it's a job */
      s = testcase_job2str(solv->pool, solv->job.elements[-intid - 1], solv->job.elements[-intid]);
    }
  else
    {
      /* it's a rule */
      s = testcase_ruleid(solv, intid);
    }
  chk = solv_chksum_create(REPOKEY_TYPE_MD5);
  solv_chksum_add(chk, s, strlen(s) + 1);
  md5 = solv_chksum_get(chk, &md5l);
  s = pool_bin2hex(solv->pool, md5, 4);
  chk = solv_chksum_free(chk, 0);
  return s;
}

static const char *
testcase_alternativeid(Solver *solv, int type, Id id, Id from)
{
  const char *s;
  Pool *pool = solv->pool;
  Chksum *chk;
  const unsigned char *md5;
  int md5l;
  chk = solv_chksum_create(REPOKEY_TYPE_MD5);
  if (type == SOLVER_ALTERNATIVE_TYPE_RECOMMENDS)
    {
      s = testcase_solvid2str(pool, from);
      solv_chksum_add(chk, s, strlen(s) + 1);
      s = testcase_dep2str(pool, id);
      solv_chksum_add(chk, s, strlen(s) + 1);
    }
  else if (type == SOLVER_ALTERNATIVE_TYPE_RULE)
    {
      s = testcase_ruleid(solv, id);
      solv_chksum_add(chk, s, strlen(s) + 1);
    }
  md5 = solv_chksum_get(chk, &md5l);
  s = pool_bin2hex(pool, md5, 4);
  chk = solv_chksum_free(chk, 0);
  return s;
}

static struct class2str {
  Id class;
  const char *str;
} class2str[] = {
  { SOLVER_TRANSACTION_ERASE,          "erase" },
  { SOLVER_TRANSACTION_INSTALL,        "install" },
  { SOLVER_TRANSACTION_REINSTALLED,    "reinstall" },
  { SOLVER_TRANSACTION_DOWNGRADED,     "downgrade" },
  { SOLVER_TRANSACTION_CHANGED,        "change" },
  { SOLVER_TRANSACTION_UPGRADED,       "upgrade" },
  { SOLVER_TRANSACTION_OBSOLETED,      "obsolete" },
  { SOLVER_TRANSACTION_MULTIINSTALL,   "multiinstall" },
  { SOLVER_TRANSACTION_MULTIREINSTALL, "multireinstall" },
  { 0, 0 }
};

static struct reason2str {
  Id reason;
  const char *str;
} reason2str[] = {
  { SOLVER_REASON_UNRELATED,		"unrelated" },
  { SOLVER_REASON_UNIT_RULE,		"unit" },
  { SOLVER_REASON_KEEP_INSTALLED,	"keep" },
  { SOLVER_REASON_RESOLVE_JOB,		"job" },
  { SOLVER_REASON_UPDATE_INSTALLED,	"update" },
  { SOLVER_REASON_CLEANDEPS_ERASE,	"cleandeps" },
  { SOLVER_REASON_RESOLVE,		"resolve" },
  { SOLVER_REASON_WEAKDEP,		"weakdep" },
  { SOLVER_REASON_RESOLVE_ORPHAN,	"orphan" },

  { SOLVER_REASON_RECOMMENDED,		"recommended" },
  { SOLVER_REASON_SUPPLEMENTED,		"supplemented" },
  { 0, 0 }
};

static const char *
testcase_reason2str(Id reason)
{
  int i;
  for (i = 0; reason2str[i].str; i++)
    if (reason == reason2str[i].reason)
      return reason2str[i].str;
  return "?";
}

static struct rclass2str {
  Id rclass;
  const char *str;
} rclass2str[] = {
  { SOLVER_RULE_PKG, "pkg" },
  { SOLVER_RULE_UPDATE, "update" },
  { SOLVER_RULE_FEATURE, "feature" },
  { SOLVER_RULE_JOB, "job" },
  { SOLVER_RULE_DISTUPGRADE, "distupgrade" },
  { SOLVER_RULE_INFARCH, "infarch" },
  { SOLVER_RULE_CHOICE, "choice" },
  { SOLVER_RULE_LEARNT, "learnt" },
  { SOLVER_RULE_BEST, "best" },
  { SOLVER_RULE_YUMOBS, "yumobs" },
  { SOLVER_RULE_BLACK, "black" },
  { SOLVER_RULE_RECOMMENDS, "recommends" },
  { SOLVER_RULE_STRICT_REPO_PRIORITY, "strictrepoprio" },
  { 0, 0 }
};

static const char *
testcase_rclass2str(Id rclass)
{
  int i;
  for (i = 0; rclass2str[i].str; i++)
    if (rclass == rclass2str[i].rclass)
      return rclass2str[i].str;
  return "unknown";
}

static int
dump_genid(Pool *pool, Strqueue *sq, Id id, int cnt)
{
  struct oplist *op;
  char cntbuf[26];
  const char *s;

  if (ISRELDEP(id))
    {
      Reldep *rd = GETRELDEP(pool, id);
      for (op = oplist; op->flags; op++)
	if (rd->flags == op->flags)
	  break;
      cnt = dump_genid(pool, sq, rd->name, cnt);
      cnt = dump_genid(pool, sq, rd->evr, cnt);
      sprintf(cntbuf, "genid %2d: genid ", cnt++);
      s = pool_tmpjoin(pool, cntbuf, "op ", op->flags ? op->opname : "unknown");
    }
  else
    {
      sprintf(cntbuf, "genid %2d: genid ", cnt++);
      s = pool_tmpjoin(pool, cntbuf, id ? "lit " : "null", id ? pool_id2str(pool, id) : 0);
    }
  strqueue_push(sq, s);
  return cnt;
}

char *
testcase_solverresult(Solver *solv, int resultflags)
{
  Pool *pool = solv->pool;
  int i, j;
  Id p, op;
  const char *s;
  char *result;
  Strqueue sq;

  strqueue_init(&sq);
  if ((resultflags & TESTCASE_RESULT_TRANSACTION) != 0)
    {
      Transaction *trans = solver_create_transaction(solv);
      Queue q;

      queue_init(&q);
      for (i = 0; class2str[i].str; i++)
	{
	  queue_empty(&q);
	  transaction_classify_pkgs(trans, SOLVER_TRANSACTION_KEEP_PSEUDO, class2str[i].class, 0, 0, &q);
	  for (j = 0; j < q.count; j++)
	    {
	      p = q.elements[j];
	      op = 0;
	      if (pool->installed && pool->solvables[p].repo == pool->installed)
		op = transaction_obs_pkg(trans, p);
	      s = pool_tmpjoin(pool, class2str[i].str, " ", testcase_solvid2str(pool, p));
	      if (op)
		s = pool_tmpjoin(pool, s, " ", testcase_solvid2str(pool, op));
	      strqueue_push(&sq, s);
	    }
	}
      queue_free(&q);
      transaction_free(trans);
    }
  if ((resultflags & TESTCASE_RESULT_PROBLEMS) != 0)
    {
      char *probprefix, *solprefix;
      int problem, solution, element;
      int pcnt, scnt;

      pcnt = solver_problem_count(solv);
      for (problem = 1; problem <= pcnt; problem++)
	{
	  Id rid, from, to, dep;
	  SolverRuleinfo rinfo;
	  rid = solver_findproblemrule(solv, problem);
	  s = testcase_problemid(solv, problem);
	  probprefix = solv_dupjoin("problem ", s, 0);
	  rinfo = solver_ruleinfo(solv, rid, &from, &to, &dep);
	  s = pool_tmpjoin(pool, probprefix, " info ", solver_problemruleinfo2str(solv, rinfo, from, to, dep));
	  strqueue_push(&sq, s);
	  scnt = solver_solution_count(solv, problem);
	  for (solution = 1; solution <= scnt; solution++)
	    {
	      s = testcase_solutionid(solv, problem, solution);
	      solprefix = solv_dupjoin(probprefix, " solution ", s);
	      element = 0;
	      while ((element = solver_next_solutionelement(solv, problem, solution, element, &p, &op)) != 0)
		{
		  if (p == SOLVER_SOLUTION_JOB)
		    s = pool_tmpjoin(pool, solprefix, " deljob ", testcase_job2str(pool, solv->job.elements[op - 1], solv->job.elements[op]));
		  else if (p > 0 && op == 0)
		    s = pool_tmpjoin(pool, solprefix, " erase ", testcase_solvid2str(pool, p));
		  else if (p > 0 && op > 0)
		    {
		      s = pool_tmpjoin(pool, solprefix, " replace ", testcase_solvid2str(pool, p));
		      s = pool_tmpappend(pool, s, " ", testcase_solvid2str(pool, op));
		    }
		  else if (p < 0 && op > 0)
		    s = pool_tmpjoin(pool, solprefix, " allow ", testcase_solvid2str(pool, op));
		  else
		    s = pool_tmpjoin(pool, solprefix, " unknown", 0);
		  strqueue_push(&sq, s);
		}
	      solv_free(solprefix);
	    }
	  solv_free(probprefix);
	}
    }

  if ((resultflags & TESTCASE_RESULT_PROOF) != 0)
    {
      char *probprefix;
      int pcnt, problem;
      Queue q, lq;

      queue_init(&q);
      queue_init(&lq);
      pcnt = solver_problem_count(solv);
      for (problem = 1; problem <= pcnt + lq.count; problem++)
	{
	  if (problem <= pcnt)
	    {
	      s = testcase_problemid(solv, problem);
	      solver_get_decisionlist(solv, problem, SOLVER_DECISIONLIST_PROBLEM, &q);
	    }
	  else
	    {
	      s = testcase_ruleid(solv, lq.elements[problem - pcnt - 1]);
	      solver_get_decisionlist(solv, lq.elements[problem - pcnt - 1], SOLVER_DECISIONLIST_LEARNTRULE, &q);
	    }
	  probprefix = solv_dupjoin("proof ", s, 0);
	  for (i = 0; i < q.count; i += 3)
	    {
	      SolverRuleinfo rclass;
	      Queue rq;
	      Id truelit = q.elements[i];
	      Id rid = q.elements[i + 2];
	      char *rprefix;
	      char nbuf[16];

	      rclass = solver_ruleclass(solv, rid);
	      if (rclass == SOLVER_RULE_LEARNT)
		queue_pushunique(&lq, rid);
	      queue_init(&rq);
	      solver_ruleliterals(solv, rid, &rq);
	      sprintf(nbuf, "%3d", i / 3);
	      rprefix = solv_dupjoin(probprefix, " ", nbuf);
	      if (q.elements[i + 1] == SOLVER_REASON_PREMISE)
		{
		  rprefix = solv_dupappend(rprefix, " premise", 0);
		  queue_empty(&rq);
		  queue_push(&rq, truelit);
		}
	      else
		{
		  rprefix = solv_dupappend(rprefix, " ", testcase_rclass2str(rclass));
		  rprefix = solv_dupappend(rprefix, " ", testcase_ruleid(solv, rid));
		}
	      strqueue_push(&sq, rprefix);
	      solv_free(rprefix);
	      rprefix = solv_dupjoin(probprefix, " ", nbuf);
	      rprefix = solv_dupappend(rprefix, ": ", 0);
	      for (j = 0; j < rq.count; j++)
		{
		  const char *s;
		  Id p = rq.elements[j];
		  if (p == truelit)
		    s = pool_tmpjoin(pool, rprefix, "-->", 0);
		  else
		    s = pool_tmpjoin(pool, rprefix, "   ", 0);
		  if (p < 0)
		    s = pool_tmpappend(pool, s, " -", testcase_solvid2str(pool, -p));
		  else
		    s = pool_tmpappend(pool, s, "  ", testcase_solvid2str(pool, p));
		  strqueue_push(&sq, s);
		}
	      solv_free(rprefix);
	      queue_free(&rq);
	    }
	  solv_free(probprefix);
	}
      queue_free(&q);
      queue_free(&lq);
    }

  if ((resultflags & TESTCASE_RESULT_ORPHANED) != 0)
    {
      Queue q;

      queue_init(&q);
      solver_get_orphaned(solv, &q);
      for (i = 0; i < q.count; i++)
	{
	  s = pool_tmpjoin(pool, "orphaned ", testcase_solvid2str(pool, q.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_free(&q);
    }

  if ((resultflags & TESTCASE_RESULT_RECOMMENDED) != 0)
    {
      Queue qr, qs;

      queue_init(&qr);
      queue_init(&qs);
      solver_get_recommendations(solv, &qr, &qs, 0);
      for (i = 0; i < qr.count; i++)
	{
	  s = pool_tmpjoin(pool, "recommended ", testcase_solvid2str(pool, qr.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      for (i = 0; i < qs.count; i++)
	{
	  s = pool_tmpjoin(pool, "suggested ", testcase_solvid2str(pool, qs.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_free(&qr);
      queue_free(&qs);
    }

  if ((resultflags & TESTCASE_RESULT_UNNEEDED) != 0)
    {
      Queue q, qf;

      queue_init(&q);
      queue_init(&qf);
      solver_get_unneeded(solv, &q, 0);
      solver_get_unneeded(solv, &qf, 1);
      for (i = j = 0; i < q.count; i++)
	{
	  /* we rely on qf containing a subset of q in the same order */
	  if (j < qf.count && q.elements[i] == qf.elements[j])
	    {
	      s = pool_tmpjoin(pool, "unneeded_filtered ", testcase_solvid2str(pool, q.elements[i]), 0);
	      j++;
	    }
	  else
	    s = pool_tmpjoin(pool, "unneeded ", testcase_solvid2str(pool, q.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_free(&q);
      queue_free(&qf);
    }
  if ((resultflags & TESTCASE_RESULT_USERINSTALLED) != 0)
    {
      Queue q;
      queue_init(&q);
      solver_get_userinstalled(solv, &q, 0);
      for (i = 0; i < q.count; i++)
	{
	  s = pool_tmpjoin(pool, "userinstalled pkg ", testcase_solvid2str(pool, q.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_empty(&q);
      solver_get_userinstalled(solv, &q, GET_USERINSTALLED_NAMES | GET_USERINSTALLED_INVERTED);
      for (i = 0; i < q.count; i++)
	{
	  s = pool_tmpjoin(pool, "autoinst name ", pool_id2str(pool, q.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_free(&q);
    }
  if ((resultflags & TESTCASE_RESULT_ORDER) != 0)
    {
      int i;
      char buf[256];
      Id p;
      Transaction *trans = solver_create_transaction(solv);
      transaction_order(trans, 0);
      for (i = 0; i < trans->steps.count; i++)
	{
	  p = trans->steps.elements[i];
	  if (pool->installed && pool->solvables[p].repo == pool->installed)
	    sprintf(buf, "%4d erase ", i + 1);
	  else
	    sprintf(buf, "%4d install ", i + 1);
	  s = pool_tmpjoin(pool, "order ", buf, testcase_solvid2str(pool, p));
	  strqueue_push(&sq, s);
	}
      transaction_free(trans);
    }
  if ((resultflags & TESTCASE_RESULT_ORDEREDGES) != 0)
    {
      Queue q;
      int i, j;
      Id p, p2;
      Transaction *trans = solver_create_transaction(solv);
      transaction_order(trans, SOLVER_TRANSACTION_KEEP_ORDEREDGES);
      queue_init(&q);
      for (i = 0; i < trans->steps.count; i++)
	{
	  p = trans->steps.elements[i];
	  transaction_order_get_edges(trans, p, &q, 1);
	  for (j = 0; j < q.count; j += 2)
	    {
	      char typebuf[32], *s;
	      p2 = q.elements[j];
	      sprintf(typebuf, " -%x-> ", q.elements[j + 1]);
	      s = pool_tmpjoin(pool, "orderedge ", testcase_solvid2str(pool, p), typebuf);
	      s = pool_tmpappend(pool, s, testcase_solvid2str(pool, p2), 0);
	      strqueue_push(&sq, s);
	    }
	}
      queue_free(&q);
      transaction_free(trans);
    }
  if ((resultflags & TESTCASE_RESULT_ALTERNATIVES) != 0)
    {
      Queue q;
      int cnt;
      Id alternative;
      queue_init(&q);
      cnt = solver_alternatives_count(solv);
      for (alternative = 1; alternative <= cnt; alternative++)
	{
	  Id id, from, chosen;
	  char num[20], *s;
	  int type = solver_get_alternative(solv, alternative, &id, &from, &chosen, &q, 0);
	  char *altprefix = solv_dupjoin("alternative ", testcase_alternativeid(solv, type, id, from), " ");
	  strcpy(num, " 0 ");
          s = pool_tmpjoin(pool, altprefix, num, solver_alternative2str(solv, type, id, from));
	  strqueue_push(&sq, s);
	  for (i = 0; i < q.count; i++)
	    {
	      Id p = q.elements[i];
	      if (i >= 9)
	        num[0] = '0' + (i + 1) / 10;
	      num[1] = '0' + (i + 1) % 10;
	      if (-p == chosen)
		s = pool_tmpjoin(pool, altprefix, num, "+ ");
	      else if (p < 0)
	        s = pool_tmpjoin(pool, altprefix, num, "- ");
	      else if (p >= 0)
	        s = pool_tmpjoin(pool, altprefix, num, "  ");
	      s = pool_tmpappend(pool, s,  testcase_solvid2str(pool, p < 0 ? -p : p), 0);
	      strqueue_push(&sq, s);
	    }
	  solv_free(altprefix);
	}
      queue_free(&q);
    }
  if ((resultflags & TESTCASE_RESULT_RULES) != 0)
    {
      /* dump all rules */
      Id rid;
      SolverRuleinfo rclass;
      Queue q;
      int i;
      char *prefix;

      queue_init(&q);
      for (rid = 1; (rclass = solver_ruleclass(solv, rid)) != SOLVER_RULE_UNKNOWN; rid++)
	{
	  solver_ruleliterals(solv, rid, &q);
	  if (rclass == SOLVER_RULE_FEATURE && q.count == 1 && q.elements[0] == -SYSTEMSOLVABLE)
	    continue;
	  prefix = solv_dupjoin("rule ", testcase_rclass2str(rclass), " ");
	  prefix = solv_dupappend(prefix, testcase_ruleid(solv, rid), 0);
	  for (i = 0; i < q.count; i++)
	    {
	      Id p = q.elements[i];
	      const char *s;
	      if (p < 0)
		s = pool_tmpjoin(pool, prefix, " -", testcase_solvid2str(pool, -p));
	      else
		s = pool_tmpjoin(pool, prefix, "  ", testcase_solvid2str(pool, p));
	      strqueue_push(&sq, s);
	    }
	  solv_free(prefix);
	}
      queue_free(&q);
    }
  if ((resultflags & TESTCASE_RESULT_GENID) != 0)
    {
      for (i = 0 ; i < solv->job.count; i += 2)
	{
	  Id id, id2;
	  if (solv->job.elements[i] != (SOLVER_NOOP | SOLVER_SOLVABLE_PROVIDES))
	    continue;
	  id = solv->job.elements[i + 1];
	  s = testcase_dep2str(pool, id);
	  s = pool_tmpjoin(pool, "genid dep ", s, 0);
	  strqueue_push(&sq, s);
	  if ((id2 = testcase_str2dep(pool, s + 10)) != id)
	    {
	      s = pool_tmpjoin(pool, "genid roundtrip error: ", testcase_dep2str(pool, id2), 0);
	      strqueue_push(&sq, s);
	    }
	  dump_genid(pool, &sq, id, 1);
	}
    }
  if ((resultflags & TESTCASE_RESULT_REASON) != 0)
    {
      Queue whyq;
      queue_init(&whyq);
      FOR_POOL_SOLVABLES(p)
	{
	  Id info, p2, id;
          int reason = solver_describe_decision(solv, p, &info);
	  if (reason == SOLVER_REASON_UNRELATED)
	    continue;
	  if (reason == SOLVER_REASON_WEAKDEP)
	    {
	      solver_describe_weakdep_decision(solv, p, &whyq);
	      if (whyq.count)
		{
		  for (i = 0; i < whyq.count; i += 3)
		    {
		      reason = whyq.elements[i];
		      p2 = whyq.elements[i + 1];
		      id = whyq.elements[i + 2];
		      s = pool_tmpjoin(pool, "reason ", testcase_solvid2str(pool, p), 0);
		      s = pool_tmpappend(pool, s, " ", testcase_reason2str(reason));
		      s = pool_tmpappend(pool, s, " ", testcase_dep2str(pool, id));
		      if (p2)
		        s = pool_tmpappend(pool, s, " ", testcase_solvid2str(pool, p2));
		      strqueue_push(&sq, s);
		    }
		  continue;
		}
	    }
	  s = pool_tmpjoin(pool, "reason ", testcase_solvid2str(pool, p), 0);
	  s = pool_tmpappend(pool, s, " ", testcase_reason2str(reason));
	  if (info)
	    s = pool_tmpappend(pool, s, " ", testcase_ruleid(solv, info));
	  strqueue_push(&sq, s);
	}
      queue_free(&whyq);
    }
  if ((resultflags & TESTCASE_RESULT_CLEANDEPS) != 0)
    {
      Queue q;

      queue_init(&q);
      solver_get_cleandeps(solv, &q);
      for (i = 0; i < q.count; i++)
	{
	  s = pool_tmpjoin(pool, "cleandeps ", testcase_solvid2str(pool, q.elements[i]), 0);
	  strqueue_push(&sq, s);
	}
      queue_free(&q);
    }
  if ((resultflags & TESTCASE_RESULT_JOBS) != 0)
    {
      for (i = 0; i < solv->job.count; i += 2)
	{
	  s = (char *)testcase_job2str(pool, solv->job.elements[i], solv->job.elements[i + 1]);
	  s = pool_tmpjoin(pool, "job ", s, 0);
	  strqueue_push(&sq, s);
	}
    }
  strqueue_sort(&sq);
  result = strqueue_join(&sq);
  strqueue_free(&sq);
  return result;
}

static void
dump_custom_vendorcheck(Pool *pool, Strqueue *sq, int (*vendorcheck)(Pool *, Solvable *, Solvable *))
{
  Id p, lastvendor = 0;
  Queue vq;
  int i, j;
  char *cmd;

  queue_init(&vq);
  FOR_POOL_SOLVABLES(p)
    {
      Id vendor = pool->solvables[p].vendor;
      if (!vendor || vendor == lastvendor)
	continue;
      lastvendor = vendor;
      for (i = 0; i < vq.count; i += 2)
	if (vq.elements[i] == vendor)
	  break;
      if (i == vq.count)
        queue_push2(&vq, vendor, p);
    }
  for (i = 0; i < vq.count; i += 2)
    {
      Solvable *s1 = pool->solvables + vq.elements[i + 1];
      for (j = i + 2; j < vq.count; j += 2)
	{
	  Solvable *s2 = pool->solvables + vq.elements[j + 1];
	  if (vendorcheck(pool, s1, s2) || vendorcheck(pool, s2, s1))
	    continue;
	  cmd = pool_tmpjoin(pool, "vendorclass", 0, 0);
	  cmd = pool_tmpappend(pool, cmd, " ", testcase_escape(pool, pool_id2str(pool, vq.elements[i])));
	  cmd = pool_tmpappend(pool, cmd, " ", testcase_escape(pool, pool_id2str(pool, vq.elements[j])));
	  strqueue_push(sq, cmd);
	}
    }
  queue_free(&vq);
}

static int
testcase_write_mangled(Solver *solv, const char *dir, int resultflags, const char *testcasename, const char *resultname)
{
  Pool *pool = solv->pool;
  Repo *repo;
  int i;
  Id arch, repoid;
  Id lowscore;
  FILE *fp;
  Strqueue sq;
  char *cmd, *out, *result;
  const char *s;
  int (*vendorcheck)(Pool *, Solvable *, Solvable *);

  if (!testcasename)
    testcasename = "testcase.t";
  if (!resultname)
    resultname = "solver.result";

#ifdef _WIN32
  if (mkdir(dir) && errno != EEXIST)
#else
  if (mkdir(dir, 0777) && errno != EEXIST)
#endif
    return pool_error(solv->pool, 0, "testcase_write: could not create directory '%s'", dir);
  strqueue_init(&sq);
  FOR_REPOS(repoid, repo)
    {
      const char *name = testcase_repoid2str(pool, repoid);
      char priobuf[50];
      if (repo->subpriority)
	sprintf(priobuf, "%d.%d", repo->priority, repo->subpriority);
      else
	sprintf(priobuf, "%d", repo->priority);
#if !defined(WITHOUT_COOKIEOPEN) && defined(ENABLE_ZLIB_COMPRESSION)
      out = pool_tmpjoin(pool, name, ".repo", ".gz");
#else
      out = pool_tmpjoin(pool, name, ".repo", 0);
#endif
      for (i = 0; out[i]; i++)
	if (out[i] == '/')
	  out[i] = '_';
      cmd = pool_tmpjoin(pool, "repo ", name, " ");
      cmd = pool_tmpappend(pool, cmd, priobuf, " ");
      cmd = pool_tmpappend(pool, cmd, "testtags ", out);
      strqueue_push(&sq, cmd);
      out = pool_tmpjoin(pool, dir, "/", out);
      if (!(fp = solv_xfopen(out, "w")))
	{
	  pool_error(solv->pool, 0, "testcase_write: could not open '%s' for writing", out);
	  strqueue_free(&sq);
	  return 0;
	}
      testcase_write_testtags(repo, fp);
      if (fclose(fp))
	{
	  pool_error(solv->pool, 0, "testcase_write: write error");
	  strqueue_free(&sq);
	  return 0;
	}
    }
  /* hmm, this is not optimal... we currently search for the lowest score */
  lowscore = 0;
  arch = pool->solvables[SYSTEMSOLVABLE].arch;
  for (i = 0; i < pool->lastarch; i++)
    {
      if (pool->id2arch[i] == 1 && !lowscore)
	arch = i;
      if (pool->id2arch[i] > 0x10000 && (!lowscore || pool->id2arch[i] < lowscore))
	{
	  arch = i;
	  lowscore = pool->id2arch[i];
	}
    }
  cmd = pool_tmpjoin(pool, "system ", pool->lastarch ? pool_id2str(pool, arch) : "-", 0);
  for (i = 0; disttype2str[i].str != 0; i++)
    if (pool->disttype == disttype2str[i].type)
      break;
  pool_tmpappend(pool, cmd, " ", disttype2str[i].str ? disttype2str[i].str : "unknown");
  if (pool->installed)
    cmd = pool_tmpappend(pool, cmd, " ", testcase_repoid2str(pool, pool->installed->repoid));
  strqueue_push(&sq, cmd);
  s = testcase_getpoolflags(solv->pool);
  if (*s)
    {
      cmd = pool_tmpjoin(pool, "poolflags ", s, 0);
      strqueue_push(&sq, cmd);
    }

  vendorcheck = pool_get_custom_vendorcheck(pool);
  if (vendorcheck)
    dump_custom_vendorcheck(pool, &sq, vendorcheck);
  else if (pool->vendorclasses)
    {
      cmd = 0;
      for (i = 0; pool->vendorclasses[i]; i++)
	{
	  cmd = pool_tmpappend(pool, cmd ? cmd : "vendorclass", " ", testcase_escape(pool, pool->vendorclasses[i]));
	  if (!pool->vendorclasses[i + 1])
	    {
	      strqueue_push(&sq, cmd);
	      cmd = 0;
	      i++;
	    }
	}
    }

  /* dump disabled packages (must come before the namespace/job lines) */
  if (pool->considered)
    {
      Id p;
      FOR_POOL_SOLVABLES(p)
	if (!MAPTST(pool->considered, p))
	  {
	    cmd = pool_tmpjoin(pool, "disable pkg ", testcase_solvid2str(pool, p), 0);
	    strqueue_push(&sq, cmd);
	  }
    }

  s = testcase_getsolverflags(solv);
  if (*s)
    {
      cmd = pool_tmpjoin(pool, "solverflags ", s, 0);
      strqueue_push(&sq, cmd);
    }

  /* now dump all the ns callback values we know */
  if (pool->nscallback)
    {
      Id rid;
      int d;
      for (rid = 1; rid < pool->nrels; rid++)
	{
	  Reldep *rd = pool->rels + rid;
	  if (rd->flags != REL_NAMESPACE || rd->name == NAMESPACE_OTHERPROVIDERS || rd->name == NAMESPACE_SPLITPROVIDES)
	    continue;
	  /* evaluate all namespace ids, skip empty results */
	  d = pool_whatprovides(pool, MAKERELDEP(rid));
	  if (!d || !pool->whatprovidesdata[d])
	    continue;
	  cmd = pool_tmpjoin(pool, "namespace ", pool_id2str(pool, rd->name), "(");
	  cmd = pool_tmpappend(pool, cmd, pool_id2str(pool, rd->evr), ")");
	  for (;  pool->whatprovidesdata[d]; d++)
	    cmd = pool_tmpappend(pool, cmd, " ", testcase_solvid2str(pool, pool->whatprovidesdata[d]));
	  strqueue_push(&sq, cmd);
	}
    }

  for (i = 0; i < solv->job.count; i += 2)
    {
      cmd = (char *)testcase_job2str(pool, solv->job.elements[i], solv->job.elements[i + 1]);
      cmd = pool_tmpjoin(pool, "job ", cmd, 0);
      strqueue_push(&sq, cmd);
    }

  if ((resultflags & ~TESTCASE_RESULT_REUSE_SOLVER) != 0)
    {
      cmd = 0;
      for (i = 0; resultflags2str[i].str; i++)
	if ((resultflags & resultflags2str[i].flag) != 0)
	  cmd = pool_tmpappend(pool, cmd, cmd ? "," : 0, resultflags2str[i].str);
      cmd = pool_tmpjoin(pool, "result ", cmd ? cmd : "?", 0);
      cmd = pool_tmpappend(pool, cmd, " ", resultname);
      strqueue_push(&sq, cmd);
      result = testcase_solverresult(solv, resultflags);
      if (!strcmp(resultname, "<inline>"))
	{
	  Strqueue rsq;
	  strqueue_init(&rsq);
	  strqueue_split(&rsq, result);
	  for (i = 0; i < rsq.nstr; i++)
	    {
	      cmd = pool_tmpjoin(pool, "#>", rsq.str[i], 0);
	      strqueue_push(&sq, cmd);
	    }
	  strqueue_free(&rsq);
	}
      else
	{
	  out = pool_tmpjoin(pool, dir, "/", resultname);
	  if (!(fp = fopen(out, "w")))
	    {
	      pool_error(solv->pool, 0, "testcase_write: could not open '%s' for writing", out);
	      solv_free(result);
	      strqueue_free(&sq);
	      return 0;
	    }
	  if (result && *result && fwrite(result, strlen(result), 1, fp) != 1)
	    {
	      pool_error(solv->pool, 0, "testcase_write: write error");
	      solv_free(result);
	      strqueue_free(&sq);
	      fclose(fp);
	      return 0;
	    }
	  if (fclose(fp))
	    {
	      pool_error(solv->pool, 0, "testcase_write: write error");
	      solv_free(result);
	      strqueue_free(&sq);
	      return 0;
	    }
	}
      solv_free(result);
    }

  result = strqueue_join(&sq);
  strqueue_free(&sq);
  out = pool_tmpjoin(pool, dir, "/", testcasename);
  if (!(fp = fopen(out, "w")))
    {
      pool_error(solv->pool, 0, "testcase_write: could not open '%s' for writing", out);
      solv_free(result);
      return 0;
    }
  if (*result && fwrite(result, strlen(result), 1, fp) != 1)
    {
      pool_error(solv->pool, 0, "testcase_write: write error");
      solv_free(result);
      fclose(fp);
      return 0;
    }
  if (fclose(fp))
    {
      pool_error(solv->pool, 0, "testcase_write: write error");
      solv_free(result);
      return 0;
    }
  solv_free(result);
  return 1;
}

const char **
testcase_mangle_repo_names(Pool *pool)
{
  int i, repoid, mangle = 1;
  Repo *repo;
  const char **names = solv_calloc(pool->nrepos, sizeof(char *));
  FOR_REPOS(repoid, repo)
    {
      char *buf, *mp;
      buf = solv_malloc((repo->name ? strlen(repo->name) : 0) + 40);
      if (!repo->name || !repo->name[0])
        sprintf(buf, "#%d", repoid);
      else
	strcpy(buf, repo->name);
      for (mp = buf; *mp; mp++)
	if (*mp == ' ' || *mp == '\t' || *mp == '/')
	  *mp = '_';
      for (i = 1; i < repoid; i++)
        {
	  if (!names[i] || strcmp(buf, names[i]) != 0)
	    continue;
          sprintf(mp, "_%d", mangle++);
	  i = 0;	/* restart conflict check */
	}
      names[repoid] = buf;
    }
  return names;
}

static void
swap_repo_names(Pool *pool, const char **names)
{
  int repoid;
  Repo *repo;
  FOR_REPOS(repoid, repo)
    {
      const char *n = repo->name;
      repo->name = names[repoid];
      names[repoid] = n;
    }
}

int
testcase_write(Solver *solv, const char *dir, int resultflags, const char *testcasename, const char *resultname)
{
  Pool *pool = solv->pool;
  int r, repoid;
  const char **names;

  /* mangle repo names so that there are no conflicts */
  names = testcase_mangle_repo_names(pool);
  swap_repo_names(pool, names);
  r = testcase_write_mangled(solv, dir, resultflags, testcasename, resultname);
  swap_repo_names(pool, names);
  for (repoid = 1; repoid < pool->nrepos; repoid++)
    solv_free((void *)names[repoid]);
  solv_free((void *)names);
  return r;
}

static char *
read_inline_file(FILE *fp, char **bufp, char **bufpp, int *buflp)
{
  char *result = solv_malloc(1024);
  char *rp = result;
  int resultl = 1024;

  for (;;)
    {
      size_t rl;
      if (rp - result + 256 >= resultl)
	{
	  resultl = rp - result;
	  result = solv_realloc(result, resultl + 1024);
	  rp = result + resultl;
	  resultl += 1024;
	}
      if (!fgets(rp, resultl - (rp - result), fp))
	*rp = 0;
      rl = strlen(rp);
      if (rl && (rp == result || rp[-1] == '\n'))
	{
	  if (rl > 1 && rp[0] == '#' && rp[1] == '>')
	    {
	      memmove(rp, rp + 2, rl - 2);
	      rl -= 2;
	    }
	  else
	    {
	      while (rl + 16 > *buflp)
		{
		  *bufp = solv_realloc(*bufp, *buflp + 512);
		  *buflp += 512;
		}
	      memmove(*bufp, rp, rl);
	      if ((*bufp)[rl - 1] == '\n')
		{
		  ungetc('\n', fp);
		  rl--;
		}
	      (*bufp)[rl] = 0;
	      (*bufpp) = *bufp + rl;
	      rl = 0;
	    }
	}
      if (rl <= 0)
	{
	  *rp = 0;
	  break;
	}
      rp += rl;
    }
  return result;
}

static char *
read_file(FILE *fp)
{
  char *result = solv_malloc(1024);
  char *rp = result;
  int resultl = 1024;

  for (;;)
    {
      size_t rl;
      if (rp - result + 256 >= resultl)
	{
	  resultl = rp - result;
	  result = solv_realloc(result, resultl + 1024);
	  rp = result + resultl;
	  resultl += 1024;
	}
      rl = fread(rp, 1, resultl - (rp - result), fp);
      if (rl <= 0)
	{
	  *rp = 0;
	  break;
	}
      rp += rl;
    }
  return result;
}

static int
str2resultflags(Pool *pool, char *s)	/* modifies the string! */
{
  int i, resultflags = 0;
  while (s)
    {
      char *se = strchr(s, ',');
      if (se)
	*se++ = 0;
      for (i = 0; resultflags2str[i].str; i++)
	if (!strcmp(s, resultflags2str[i].str))
	  {
	    resultflags |= resultflags2str[i].flag;
	    break;
	  }
      if (!resultflags2str[i].str)
	pool_error(pool, 0, "result: unknown flag '%s'", s);
      s = se;
    }
  return resultflags;
}

Solver *
testcase_read(Pool *pool, FILE *fp, const char *testcase, Queue *job, char **resultp, int *resultflagsp)
{
  Solver *solv;
  char *buf, *bufp;
  int bufl;
  char *testcasedir, *s;
  int l;
  char **pieces = 0;
  int npieces = 0;
  int prepared = 0;
  int closefp = !fp;
  int poolflagsreset = 0;
  int missing_features = 0;
  Id *genid = 0;
  int ngenid = 0;
  Queue autoinstq;
  int oldjobsize = job ? job->count : 0;

  if (resultp)
    *resultp = 0;
  if (resultflagsp)
    *resultflagsp = 0;
  if (!fp && !(fp = fopen(testcase, "r")))
    {
      pool_error(pool, 0, "testcase_read: could not open '%s'", testcase);
      return 0;
    }
  testcasedir = solv_strdup(testcase);
  s = strrchr(testcasedir, '/');
#ifdef _WIN32
  buf = strrchr(testcasedir, '\\');
  if (!s || (buf && buf > s))
    s = buf;
#endif
  if (s)
    s[1] = 0;
  else
    *testcasedir = 0;
  bufl = 1024;
  buf = solv_malloc(bufl);
  bufp = buf;
  solv = 0;
  queue_init(&autoinstq);
  for (;;)
    {
      if (bufp - buf + 16 > bufl)
	{
	  bufl = bufp - buf;
	  buf = solv_realloc(buf, bufl + 512);
	  bufp = buf + bufl;
          bufl += 512;
	}
      if (!fgets(bufp, bufl - (bufp - buf), fp))
	break;
      bufp = buf;
      l = strlen(buf);
      if (!l || buf[l - 1] != '\n')
	{
	  bufp += l;
	  continue;
	}
      buf[--l] = 0;
      s = buf;
      while (*s && (*s == ' ' || *s == '\t'))
	s++;
      if (!*s || *s == '#')
	continue;
      npieces = 0;
      /* split it in pieces */
      for (;;)
	{
	  while (*s == ' ' || *s == '\t')
	    s++;
	  if (!*s)
	    break;
	  pieces = solv_extend(pieces, npieces, 1, sizeof(*pieces), 7);
	  pieces[npieces++] = s;
	  while (*s && *s != ' ' && *s != '\t')
	    s++;
	  if (*s)
	    *s++ = 0;
	}
      pieces = solv_extend(pieces, npieces, 1, sizeof(*pieces), 7);
      pieces[npieces] = 0;
      if (!strcmp(pieces[0], "repo") && npieces >= 4)
	{
	  Repo *repo = repo_create(pool, pieces[1]);
	  FILE *rfp;
	  int prio, subprio;
	  const char *rdata;

	  if (pool->considered)
	    {
	      pool_error(pool, 0, "testcase_read: cannot add repos after packages were disabled");
	      continue;
	    }
	  if (solv)
	    {
	      pool_error(pool, 0, "testcase_read: cannot add repos after the solver was created");
	      continue;
	    }
	  if (job && job->count != oldjobsize)
	    {
	      pool_error(pool, 0, "testcase_read: cannot add repos after jobs have been created");
	      continue;
	    }
	  prepared = 0;
          if (!poolflagsreset)
	    {
	      poolflagsreset = 1;
	      testcase_resetpoolflags(pool);	/* hmm */
	    }
	  if (sscanf(pieces[2], "%d.%d", &prio, &subprio) != 2)
	    {
	      subprio = 0;
	      prio = atoi(pieces[2]);
	    }
          repo->priority = prio;
          repo->subpriority = subprio;
	  if (strcmp(pieces[3], "empty") != 0 && npieces > 4)
	    {
	      const char *repotype = pool_tmpjoin(pool, pieces[3], 0, 0);	/* gets overwritten in <inline> case */
	      if (!strcmp(pieces[4], "<inline>"))
		{
		  char *idata = read_inline_file(fp, &buf, &bufp, &bufl);
		  rdata = "<inline>";
		  rfp = solv_fmemopen(idata, strlen(idata), "rf");
		}
	      else
		{
		  rdata = pool_tmpjoin(pool, testcasedir, pieces[4], 0);
		  rfp = solv_xfopen(rdata, "r");
		}
	      if (!rfp)
		{
		  pool_error(pool, 0, "testcase_read: could not open '%s'", rdata);
		}
	      else if (!strcmp(repotype, "testtags"))
		{
		  testcase_add_testtags(repo, rfp, 0);
		  fclose(rfp);
		}
	      else if (!strcmp(repotype, "solv"))
		{
		  repo_add_solv(repo, rfp, 0);
		  fclose(rfp);
		}
#if ENABLE_TESTCASE_HELIXREPO
	      else if (!strcmp(repotype, "helix"))
		{
		  repo_add_helix(repo, rfp, 0);
		  fclose(rfp);
		}
#endif
	      else
		{
		  fclose(rfp);
		  pool_error(pool, 0, "testcase_read: unknown repo type for repo '%s'", repo->name);
		}
	    }
	}
      else if (!strcmp(pieces[0], "system") && npieces >= 3)
	{
	  int i;

	  /* must set the disttype before the arch */
	  if (job && job->count != oldjobsize)
	    {
	      pool_error(pool, 0, "testcase_read: cannot change the system after jobs have been created");
	      continue;
	    }
	  prepared = 0;
	  if (strcmp(pieces[2], "*") != 0)
	    {
	      char *dp = pieces[2];
	      while (dp && *dp)
		{
		  char *dpe = strchr(dp, ',');
		  if (dpe)
		    *dpe = 0;
		  for (i = 0; disttype2str[i].str != 0; i++)
		    if (!strcmp(disttype2str[i].str, dp))
		      break;
		  if (dpe)
		    *dpe++ = ',';
		  if (disttype2str[i].str)
		    {
#ifdef MULTI_SEMANTICS
		      if (pool->disttype != disttype2str[i].type)
		        pool_setdisttype(pool, disttype2str[i].type);
#endif
		      if (pool->disttype == disttype2str[i].type)
			break;
		    }
		  dp = dpe;
		}
	      if (!(dp && *dp))
		{
		  pool_error(pool, 0, "testcase_read: system: could not change disttype to '%s'", pieces[2]);
		  missing_features = 1;
		}
	    }
	  if (strcmp(pieces[1], "unset") == 0 || strcmp(pieces[1], "-") == 0)
	    pool_setarch(pool, 0);
	  else if (pieces[1][0] == ':')
	    pool_setarchpolicy(pool, pieces[1] + 1);
	  else
	    pool_setarch(pool, pieces[1]);
	  if (npieces > 3)
	    {
	      Repo *repo = testcase_str2repo(pool, pieces[3]);
	      if (!repo)
	        pool_error(pool, 0, "testcase_read: system: unknown repo '%s'", pieces[3]);
	      else
		pool_set_installed(pool, repo);
	    }
	}
      else if (!strcmp(pieces[0], "job") && npieces > 1)
	{
	  char *sp;
	  Id how, what;
	  if (prepared <= 0)
	    {
	      pool_addfileprovides(pool);
	      pool_createwhatprovides(pool);
	      prepared = 1;
	    }
	  if (npieces >= 3 && !strcmp(pieces[2], "selection"))
	    {
	      addselectionjob(pool, pieces + 1, npieces - 1, job, 0, 0);
	      continue;
	    }
	  if (npieces >= 4 && !strcmp(pieces[2], "selection_matchdeps"))
	    {
	      pieces[2] = pieces[1];
	      addselectionjob(pool, pieces + 2, npieces - 2, job, SELECTIONJOB_MATCHDEPS, pool_str2id(pool, pieces[3], 1));
	      continue;
	    }
	  if (npieces >= 4 && !strcmp(pieces[2], "selection_matchdepid"))
	    {
	      pieces[2] = pieces[1];
	      addselectionjob(pool, pieces + 2, npieces - 2, job, SELECTIONJOB_MATCHDEPID, pool_str2id(pool, pieces[3], 1));
	      continue;
	    }
	  if (npieces >= 4 && !strcmp(pieces[2], "selection_matchsolvable"))
	    {
	      pieces[2] = pieces[1];
	      addselectionjob(pool, pieces + 2, npieces - 2, job, SELECTIONJOB_MATCHSOLVABLE, pool_str2id(pool, pieces[3], 1));
	      continue;
	    }
	  /* rejoin */
	  for (sp = pieces[1]; sp < pieces[npieces - 1]; sp++)
	    if (*sp == 0)
	      *sp = ' ';
	  how = testcase_str2job(pool, pieces[1], &what);
	  if (how >= 0 && job)
	    queue_push2(job, how, what);
	}
      else if (!strcmp(pieces[0], "vendorclass") && npieces > 1)
	{
	  int i;
	  for (i = 1; i < npieces; i++)
	    testcase_unescape_inplace(pieces[i]);
	  pool_addvendorclass(pool, (const char **)(pieces + 1));
	}
      else if (!strcmp(pieces[0], "namespace") && npieces > 1)
	{
	  int i = strlen(pieces[1]);
	  s = strchr(pieces[1], '(');
	  if (!s || pieces[1][i - 1] != ')')
	    {
	      pool_error(pool, 0, "testcase_read: bad namespace '%s'", pieces[1]);
	    }
	  else
	    {
	      Id name, evr, id;
	      Queue q;
	      queue_init(&q);
	      *s = 0;
	      pieces[1][i - 1] = 0;
	      name = pool_str2id(pool, pieces[1], 1);
	      evr = pool_str2id(pool, s + 1, 1);
	      *s = '(';
	      pieces[1][i - 1] = ')';
	      id = pool_rel2id(pool, name, evr, REL_NAMESPACE, 1);
	      for (i = 2; i < npieces; i++)
		queue_push(&q, testcase_str2solvid(pool, pieces[i]));
	      /* now do the callback */
	      if (prepared <= 0)
		{
		  pool_addfileprovides(pool);
		  pool_createwhatprovides(pool);
		  prepared = 1;
		}
	      pool->whatprovides_rel[GETRELID(id)] = pool_queuetowhatprovides(pool, &q);
	      queue_free(&q);
	    }
	}
      else if (!strcmp(pieces[0], "poolflags"))
        {
	  int i;
          if (!poolflagsreset)
	    {
	      poolflagsreset = 1;
	      testcase_resetpoolflags(pool);	/* hmm */
	    }
	  for (i = 1; i < npieces; i++)
	    testcase_setpoolflags(pool, pieces[i]);
        }
      else if (!strcmp(pieces[0], "solverflags") && npieces > 1)
        {
	  int i;
	  if (!solv)
	    {
	      solv = solver_create(pool);
	      testcase_resetsolverflags(solv);
	    }
	  for (i = 1; i < npieces; i++)
	    testcase_setsolverflags(solv, pieces[i]);
        }
      else if (!strcmp(pieces[0], "result") && npieces > 1)
	{
	  char *result = 0;
	  int resultflags = str2resultflags(pool, pieces[1]);
	  const char *rdata;
	  if (npieces > 2)
	    {
	      rdata = pool_tmpjoin(pool, testcasedir, pieces[2], 0);
	      if (!strcmp(pieces[2], "<inline>"))
		result = read_inline_file(fp, &buf, &bufp, &bufl);
	      else
		{
		  FILE *rfp = fopen(rdata, "r");
		  if (!rfp)
		    pool_error(pool, 0, "testcase_read: could not open '%s'", rdata);
		  else
		    {
		      result = read_file(rfp);
		      fclose(rfp);
		    }
		}
	    }
	  if (resultp)
	  {
	    solv_free(*resultp);
	    *resultp = result;
	  }
	  else
	    solv_free(result);
	  if (resultflagsp)
	    *resultflagsp = resultflags;
	}
      else if (!strcmp(pieces[0], "nextjob"))
	{
	  if (npieces == 2 && resultflagsp && !strcmp(pieces[1], "reusesolver"))
	    *resultflagsp |= TESTCASE_RESULT_REUSE_SOLVER;
	  break;
	}
      else if (!strcmp(pieces[0], "disable") && npieces == 3)
	{
	  Id p, pp, jobsel, what = 0;
	  if (!prepared)
	    pool_createwhatprovides(pool);
	  prepared = -1;
	  if (!pool->considered)
	    {
	      pool->considered = solv_calloc(1, sizeof(Map));
	      map_init(pool->considered, pool->nsolvables);
	      map_setall(pool->considered);
	    }
	  jobsel = testcase_str2jobsel(pool, "disable", pieces + 1, npieces - 1, &what);
	  if (jobsel < 0)
	    continue;
	  if (jobsel == SOLVER_SOLVABLE_ALL)
	    map_empty(pool->considered);
	  else if (jobsel == SOLVER_SOLVABLE_REPO)
	    {
	      Repo *repo = pool_id2repo(pool, what);
	      Solvable *s;
	      FOR_REPO_SOLVABLES(repo, p, s)
		MAPCLR(pool->considered, p);
	    }
	  FOR_JOB_SELECT(p, pp, jobsel, what)
	    MAPCLR(pool->considered, p);
	}
      else if (!strcmp(pieces[0], "feature"))
	{
	  int i, j;
	  for (i = 1; i < npieces; i++)
	    {
	      for (j = 0; features[j]; j++)
		if (!strcmp(pieces[i], features[j]))
		  break;
	      if (!features[j])
		{
		  pool_error(pool, 0, "testcase_read: missing feature '%s'", pieces[i]);
		  missing_features++;
		}
	    }
	  if (missing_features)
	    break;
	}
      else if (!strcmp(pieces[0], "genid") && npieces > 1)
	{
	  Id id;
	  /* rejoin */
	  if (npieces > 2)
	    {
	      char *sp;
	      for (sp = pieces[2]; sp < pieces[npieces - 1]; sp++)
	        if (*sp == 0)
	          *sp = ' ';
	    }
	  genid = solv_extend(genid, ngenid, 1, sizeof(*genid), 7);
	  if (!strcmp(pieces[1], "op") && npieces > 2)
	    {
	      struct oplist *op;
	      for (op = oplist; op->flags; op++)
		if (!strncmp(pieces[2], op->opname, strlen(op->opname)))
		  break;
	      if (!op->flags)
		{
		  pool_error(pool, 0, "testcase_read: genid: unknown op '%s'", pieces[2]);
		  break;
		}
	      if (ngenid < 2)
		{
		  pool_error(pool, 0, "testcase_read: genid: out of stack");
		  break;
		}
	      ngenid -= 2;
	      id = pool_rel2id(pool, genid[ngenid] , genid[ngenid + 1], op->flags, 1);
	    }
	  else if (!strcmp(pieces[1], "lit"))
	    id = pool_str2id(pool, npieces > 2 ? pieces[2] : "", 1);
	  else if (!strcmp(pieces[1], "null"))
	    id = 0;
	  else if (!strcmp(pieces[1], "dep"))
	    id = testcase_str2dep(pool, pieces[2]);
	  else
	    {
	      pool_error(pool, 0, "testcase_read: genid: unknown command '%s'", pieces[1]);
	      break;
	    }
	  genid[ngenid++] = id;
	}
      else if (!strcmp(pieces[0], "autoinst") && npieces > 2)
	{
	  if (strcmp(pieces[1], "name"))
	    {
	      pool_error(pool, 0, "testcase_read: autoinst: illegal mode");
	      break;
	    }
	  queue_push(&autoinstq, pool_str2id(pool, pieces[2], 1));
	}
      else if (!strcmp(pieces[0], "evrcmp") && npieces == 3)
	{
	  Id evr1 = pool_str2id(pool, pieces[1], 1);
	  Id evr2 = pool_str2id(pool, pieces[2], 1);
	  int r = pool_evrcmp(pool, evr1, evr2, EVRCMP_COMPARE);
	  r = r < 0 ? REL_LT : r > 0 ? REL_GT : REL_EQ;
	  queue_push2(job, SOLVER_NOOP | SOLVER_SOLVABLE_PROVIDES, pool_rel2id(pool, evr1, evr2, r, 1));
	}
      else
	{
	  pool_error(pool, 0, "testcase_read: cannot parse command '%s'", pieces[0]);
	}
    }
  while (job && ngenid > 0)
    queue_push2(job, SOLVER_NOOP | SOLVER_SOLVABLE_PROVIDES, genid[--ngenid]);
  if (autoinstq.count)
    pool_add_userinstalled_jobs(pool, &autoinstq, job, GET_USERINSTALLED_NAMES | GET_USERINSTALLED_INVERTED);
  queue_free(&autoinstq);
  genid = solv_free(genid);
  buf = solv_free(buf);
  pieces = solv_free(pieces);
  solv_free(testcasedir);
  if (!prepared)
    {
      pool_addfileprovides(pool);
      pool_createwhatprovides(pool);
    }
  if (!solv)
    {
      solv = solver_create(pool);
      testcase_resetsolverflags(solv);
    }
  if (closefp)
    fclose(fp);
  if (missing_features)
    {
      solver_free(solv);
      solv = 0;
      if (resultflagsp)
	*resultflagsp = 77;	/* hack for testsolv */
    }
  return solv;
}

char *
testcase_resultdiff(const char *result1, const char *result2)
{
  Strqueue sq1, sq2, osq;
  char *r;
  strqueue_init(&sq1);
  strqueue_init(&sq2);
  strqueue_init(&osq);
  strqueue_split(&sq1, result1);
  strqueue_split(&sq2, result2);
  strqueue_sort(&sq1);
  strqueue_sort(&sq2);
  strqueue_diff(&sq1, &sq2, &osq);
  r = osq.nstr ? strqueue_join(&osq) : 0;
  strqueue_free(&sq1);
  strqueue_free(&sq2);
  strqueue_free(&osq);
  return r;
}

