/*
 * Copyright (c) 2018 Todd C. Miller <Todd.Miller@sudo.ws>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Convert from sudoers format to other formats.
 * Currently outputs to JSON
 */

#include <config.h>

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif /* HAVE_STRING_H */
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif /* HAVE_STRINGS_H */
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sudoers.h"
#include "interfaces.h"
#include "parse.h"
#include "redblack.h"
#include "sudoers_version.h"
#include "sudo_conf.h"
#include <gram.h>

#ifdef HAVE_GETOPT_LONG
# include <getopt.h>
# else
# include "compat/getopt.h"
#endif /* HAVE_GETOPT_LONG */

extern bool export_sudoers(const char *, const char *);

/*
 * Globals
 */
struct sudo_user sudo_user;
struct passwd *list_pw;
static const char short_opts[] =  "f:ho:V";
static struct option long_opts[] = {
    { "format",		required_argument,	NULL,	'f' },
    { "help",		no_argument,		NULL,	'h' },
#ifdef notyet
    { "input-format",	required_argument,	NULL,	'i' },
#endif
    { "output",		required_argument,	NULL,	'o' },
    { "version",	no_argument,		NULL,	'V' },
    { NULL,		no_argument,		NULL,	'\0' },
};

__dso_public int main(int argc, char *argv[]);
static void get_hostname(void);
static void help(void) __attribute__((__noreturn__));
static void usage(int);

int
main(int argc, char *argv[])
{
    int ch, exitcode = EXIT_FAILURE;
    const char *input_file = "-", *output_file = "-";
    const char *output_format = "JSON";
    debug_decl(main, SUDOERS_DEBUG_MAIN)

#if defined(SUDO_DEVEL) && defined(__OpenBSD__)
    {
	extern char *malloc_options;
	malloc_options = "S";
    }
#endif

    initprogname(argc > 0 ? argv[0] : "cvtsudoers");
    if (!sudoers_initlocale(setlocale(LC_ALL, ""), def_sudoers_locale))
	sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
    sudo_warn_set_locale_func(sudoers_warn_setlocale);
    bindtextdomain("sudoers", LOCALEDIR); /* XXX - should have visudo domain */
    textdomain("sudoers");

#if 0
    /* Register fatal/fatalx callback. */
    sudo_fatal_callback_register(cvtsudoers_cleanup);
#endif

    /* Read debug and plugin sections of sudo.conf. */
    if (sudo_conf_read(NULL, SUDO_CONF_DEBUG|SUDO_CONF_PLUGINS) == -1)
	goto done;

    /* Initialize the debug subsystem. */
    if (!sudoers_debug_register(getprogname(), sudo_conf_debug_files(getprogname())))
	goto done;

    /*
     * Arg handling.
     */
    while ((ch = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
	switch (ch) {
	    case 'f':
		if (strcasecmp(optarg, "json") != 0) {
		    sudo_warnx("unsupported output format %s", optarg);
		    usage(1);
		}
		output_format = optarg;
		break;
	    case 'h':
		help();
		break;
	    case 'o':
		output_file = optarg;
		break;
	    case 'V':
		(void) printf(_("%s version %s\n"), getprogname(),
		    PACKAGE_VERSION);
		(void) printf(_("%s grammar version %d\n"), getprogname(),
		    SUDOERS_GRAMMAR_VERSION);
		exitcode = EXIT_SUCCESS;
		goto done;
	    default:
		usage(1);
	}
    }
    argc -= optind;
    argv += optind;

    /* Input file (defaults to stdin). */
    if (argc > 0) {
	/* XXX - allow multiple input files? */
	if (argc > 1)
	    usage(1);
	input_file  = argv[0];
    }

    /* Mock up a fake sudo_user struct. */
    /* XXX - common with visudo */
    user_cmnd = user_base = "";
    if (geteuid() == 0) {
	const char *user = getenv("SUDO_USER");
	if (user != NULL && *user != '\0')
	    sudo_user.pw = sudo_getpwnam(user);
    }
    if (sudo_user.pw == NULL) {
	if ((sudo_user.pw = sudo_getpwuid(getuid())) == NULL)
	    sudo_fatalx(U_("you do not exist in the %s database"), "passwd");
    }
    get_hostname();

    /* Setup defaults data structures. */
    if (!init_defaults())
	sudo_fatalx(U_("unable to initialize sudoers default values"));

    exitcode = export_sudoers(input_file, output_file) ? EXIT_SUCCESS : EXIT_FAILURE;

done:
    sudo_debug_exit_int(__func__, __FILE__, __LINE__, sudo_debug_subsys, exitcode);
    return exitcode;
}

FILE *
open_sudoers(const char *sudoers, bool doedit, bool *keepopen)
{
    return fopen(sudoers, "r");
}

/* XXX - Common stubs belong in their own file */

/* STUB */
bool
init_envtables(void)
{
    return true;
}

/* STUB */
bool
user_is_exempt(void)
{
    return false;
}

/* STUB */
void
sudo_setspent(void)
{
    return;
}

/* STUB */
void
sudo_endspent(void)
{
    return;
}

/* STUB */
int
group_plugin_query(const char *user, const char *group, const struct passwd *pw)
{
    return false;
}

/* STUB */
struct interface_list *
get_interfaces(void)
{
    static struct interface_list dummy = SLIST_HEAD_INITIALIZER(interfaces);
    return &dummy;
}

/*
 * Look up the hostname and set user_host and user_shost.
 */
static void
get_hostname(void)
{
    char *p;
    debug_decl(get_hostname, SUDOERS_DEBUG_UTIL)

    if ((user_host = sudo_gethostname()) != NULL) {
	if ((p = strchr(user_host, '.'))) {
	    *p = '\0';
	    if ((user_shost = strdup(user_host)) == NULL)
		sudo_fatalx(U_("%s: %s"), __func__, U_("unable to allocate memory"));
	    *p = '.';
	} else {
	    user_shost = user_host;
	}
    } else {
	user_host = user_shost = "localhost";
    }
    user_runhost = user_host;
    user_srunhost = user_shost;
    debug_return;
}

static void
usage(int fatal)
{
    (void) fprintf(fatal ? stderr : stdout,
	"usage: %s [-hV] [-f format] [-o output_file] [sudoers_file]\n",
	    getprogname());
    if (fatal)
	exit(1);
}

static void
help(void)
{
    (void) printf(_("%s - convert between sudoers file formats\n\n"), getprogname());
    usage(0);
    (void) puts(_("\nOptions:\n"
	"  -f, --format=JSON        specify output format\n"
	"  -h, --help               display help message and exit\n"
	"  -o, --output=output_file write sudoers in JSON format to output_file\n"
	"  -V, --version            display version information and exit"));
    exit(0);
}
