/*
 * Peony
 *
 * Copyright (C) 1999, 2000 Red Hat, Inc.
 * Copyright (C) 1999, 2000 Eazel, Inc.
 *
 * Peony is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * Peony is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors: Elliot Lee <sopwith@redhat.com>,
 *          Darin Adler <darin@bentspoon.com>,
 *          John Sullivan <sullivan@eazel.com>,
 *          Cosimo Cecchi <cosimoc@gnome.org>
 *
 */

/* peony-main.c: Implementation of the routines that drive program lifecycle and main window creation/destruction. */

#include <config.h>
#include "peony-window.h"
#include <dlfcn.h>
#include <signal.h>
#include <eel/eel-debug.h>
#include <eel/eel-glib-extensions.h>
#include <eel/eel-self-checks.h>
#include <libegg/eggdesktopfile.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <libpeony-private/peony-debug-log.h>
#include <libpeony-private/peony-global-preferences.h>
#include <libpeony-private/peony-icon-names.h>
#include <libxml/parser.h>
#ifdef HAVE_LOCALE_H
	#include <locale.h>
#endif
#ifdef HAVE_MALLOC_H
	#include <malloc.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_EXEMPI
	#include <exempi/xmp.h>
#endif


static void dump_debug_log (void)
{
    char *filename;

    filename = g_build_filename (g_get_home_dir (), "peony-debug-log.txt", NULL);
    peony_debug_log_dump (filename, NULL); /* NULL GError */
    g_free (filename);
}

static int debug_log_pipes[2];

static gboolean debug_log_io_cb (GIOChannel *io, GIOCondition condition, gpointer data)
{
    char a;

    while (read (debug_log_pipes[0], &a, 1) != 1)
        ;

    peony_debug_log (TRUE, PEONY_DEBUG_LOG_DOMAIN_USER,
                    "user requested dump of debug log");

    dump_debug_log ();
    return FALSE;
}

static void sigusr1_handler (int sig)
{
    while (write (debug_log_pipes[1], "a", 1) != 1)
        ;
}

/* This is totally broken as we're using non-signal safe
 * calls in sigfatal_handler. Disable by default. */
#ifdef USE_SEGV_HANDLER

/* sigaction structures for the old handlers of these signals */
static struct sigaction old_segv_sa;
static struct sigaction old_abrt_sa;
static struct sigaction old_trap_sa;
static struct sigaction old_fpe_sa;
static struct sigaction old_bus_sa;

static void
sigfatal_handler (int sig)
{
    void (* func) (int);

    /* FIXME: is this totally busted?  We do malloc() inside these functions,
     * and yet we are inside a signal handler...
     */
    peony_debug_log (TRUE, PEONY_DEBUG_LOG_DOMAIN_USER,
                    "debug log dumped due to signal %d", sig);
    dump_debug_log ();

    switch (sig)
    {
    case SIGSEGV:
        func = old_segv_sa.sa_handler;
        break;

    case SIGABRT:
        func = old_abrt_sa.sa_handler;
        break;

    case SIGTRAP:
        func = old_trap_sa.sa_handler;
        break;

    case SIGFPE:
        func = old_fpe_sa.sa_handler;
        break;

    case SIGBUS:
        func = old_bus_sa.sa_handler;
        break;

    default:
        func = NULL;
        break;
    }

    /* this scares me */
    if (func != NULL && func != SIG_IGN && func != SIG_DFL)
        (* func) (sig);
}
#endif

static void
setup_debug_log_signals (void)
{
    struct sigaction sa;
    GIOChannel *io;

    if (pipe (debug_log_pipes) == -1)
        g_error ("Could not create pipe() for debug log");

    io = g_io_channel_unix_new (debug_log_pipes[0]);
    g_io_add_watch (io, G_IO_IN, debug_log_io_cb, NULL);

    sa.sa_handler = sigusr1_handler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction (SIGUSR1, &sa, NULL);

    /* This is totally broken as we're using non-signal safe
     * calls in sigfatal_handler. Disable by default. */
#ifdef USE_SEGV_HANDLER
    sa.sa_handler = sigfatal_handler;
    sigemptyset (&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGSEGV, &sa, &old_segv_sa);
    sigaction(SIGABRT, &sa, &old_abrt_sa);
    sigaction(SIGTRAP, &sa, &old_trap_sa);
    sigaction(SIGFPE,  &sa, &old_fpe_sa);
    sigaction(SIGBUS,  &sa, &old_bus_sa);
#endif
}

static GLogFunc default_log_handler;

static void
log_override_cb (const gchar   *log_domain,
                 GLogLevelFlags log_level,
                 const gchar   *message,
                 gpointer       user_data)
{
    gboolean is_debug;
    gboolean is_milestone;

    is_debug = ((log_level & G_LOG_LEVEL_DEBUG) != 0);
    is_milestone = !is_debug;

    peony_debug_log (is_milestone, PEONY_DEBUG_LOG_DOMAIN_GLOG, "%s", message);

    if (!is_debug)
        (* default_log_handler) (log_domain, log_level, message, user_data);
}

static void
setup_debug_log_glog (void)
{
    default_log_handler = g_log_set_default_handler (log_override_cb, NULL);
}

static void
setup_debug_log (void)
{
    char *config_filename;

    config_filename = g_build_filename (g_get_home_dir (), "peony-debug-log.conf", NULL);
    peony_debug_log_load_configuration (config_filename, NULL); /* NULL GError */
    g_free (config_filename);

    setup_debug_log_signals ();
    setup_debug_log_glog ();
}

static gboolean
running_in_ukui (void)
{
    return (g_strcmp0 (g_getenv ("XDG_CURRENT_DESKTOP"), "UKUI") == 0)
        || (g_strcmp0 (g_getenv ("XDG_SESSION_DESKTOP"), "UKUI") == 0)
        || (g_strcmp0 (g_getenv ("DESKTOP_SESSION"), "UKUI") == 0);
}

static gboolean
running_as_root (void)
{
    return geteuid () == 0;
}

int
main (int argc, char *argv[])
{
	gint retval;
    PeonyApplication *application;

    GSettings *settings = g_settings_new("org.ukui.peony.preferences");
    g_settings_set_int (settings, "peony-search",0);

#if defined (HAVE_MALLOPT) && defined(M_MMAP_THRESHOLD)
	/* Peony uses lots and lots of small and medium size allocations,
	 * and then a few large ones for the desktop background. By default
	 * glibc uses a dynamic treshold for how large allocations should
	 * be mmaped. Unfortunately this triggers quickly for peony when
	 * it does the desktop background allocations, raising the limit
	 * such that a lot of temporary large allocations end up on the
	 * heap and are thus not returned to the OS. To fix this we set
	 * a hardcoded limit. I don't know what a good value is, but 128K
	 * was the old glibc static limit, lets use that.
	 */
	mallopt (M_MMAP_THRESHOLD, 128 *1024);
#endif

#if !GLIB_CHECK_VERSION (2, 42, 0)
    /* This will be done by gtk+ later, but for now, force it to UKUI */
    g_desktop_app_info_set_desktop_env ("UKUI");
#endif

	if (g_getenv ("PEONY_DEBUG") != NULL) {
		eel_make_warnings_and_criticals_stop_in_debugger ();
	}
	
	/* Initialize gettext support */
	bindtextdomain (GETTEXT_PACKAGE, UKUILOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	g_set_prgname ("peony");

	gdk_set_allowed_backends ("x11");

	if (g_file_test (DATADIR "/applications/peony.desktop", G_FILE_TEST_EXISTS)) {
		egg_set_desktop_file (DATADIR "/applications/peony.desktop");
	}
	
#ifdef HAVE_EXEMPI
	xmp_init();
#endif

	setup_debug_log ();

	/* Initialize the services that we use. */
	LIBXML_TEST_VERSION

    /* Run the peony application. */
    application = peony_application_new();

    retval = g_application_run (G_APPLICATION (application),
                                argc, argv);

    g_object_unref (application);

 	eel_debug_shut_down ();

	return retval;
}


