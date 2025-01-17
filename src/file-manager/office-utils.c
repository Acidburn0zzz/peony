/*
 *  Peony
 *
 *  Copyright (C) 2019, Tianjin KYLIN Information Technology Co., Ltd.
 *
 *  Peony is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  Peony is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *  Modified by: quankang <quankang@kylinos.cn>
 */

#include "mime-utils.h"
#include "office-utils.h"
#include <gdk/gdkx.h>
#include <glib/gstdio.h>

#include <libpeony-private/peony-signaller.h>

static PeonyWindowInfo *current_window;
static char* todo_filename;

//static GPid old_pid = -1;

static gboolean is_busy = FALSE;

void clean_cache_files_anyway ();

char* get_html_tmp_name (char* filename);
char* get_pdf_tmp_name(char* filename);

void prepare_to_trans_file_by_window (PeonyWindowInfo* window, char* filename);
void excel2html_by_window_internal (PeonyWindowInfo *window, char *html_filename, char *excel_filename);
void office2pdf_by_window_internal (PeonyWindowInfo *window, char *pdf_filename, char* office_filename);
void excel2html_by_window_internal_unoconv (PeonyWindowInfo *window, char *html_filename, char *excel_filename);
void office2pdf_by_window_internal_unoconv (PeonyWindowInfo *window, char *pdf_filename, char* office_filename);

void clean_cache_files_anyway (){
    char* tmp_path;
    tmp_path = g_build_filename (g_get_user_cache_dir (), "peony", NULL);
    if(g_remove(tmp_path) != 0){
        //printf ("remove cache path failed: %s\n",tmp_path);
		char* cmd = g_strdup_printf ("rm -rf %s",tmp_path);
		//printf ("cmd: %s", cmd);
        system (cmd);
    }
    g_free (tmp_path);
}

gboolean is_office_busy(){
	return is_busy;
}

static void trans_next_file (PeonyWindowInfo* window) {

	if (!todo_filename){
		//printf ("no next file\n");
		return;
	}

	//printf ("next file\n\n\n\n\n");

	prepare_to_trans_file_by_window (window, todo_filename);

	todo_filename = NULL;
}

void office_utils_conncet_window_info (PeonyWindowInfo *window_info) {
	g_signal_connect (window_info, "office_trans_ready", G_CALLBACK (trans_next_file), todo_filename);
}

void office_utils_disconnect_window_info (PeonyWindowInfo *window_info) {
	g_signal_handlers_disconnect_by_func (window_info, G_CALLBACK (trans_next_file), todo_filename);
}

void prepare_to_trans_file_by_window (PeonyWindowInfo* window, char* filename) {
	//printf ("prepare_to_trans_file_by_window\n");
	if (!is_busy) {
		is_busy = TRUE;

		//printf ("...................\n");
		char *doing_filename = peony_navigation_window_get_current_previewing_office_file_by_window_info (window);
		//printf ("doing %s\n",doing_filename);
		char *pending_preview_filename = get_pending_preview_filename(doing_filename);
		//printf ("pending: %s\n", pending_preview_filename);
		peony_navigation_window_set_pending_preview_file_by_window_info (window, pending_preview_filename);
		g_free (pending_preview_filename);
		pending_preview_filename = peony_navigation_window_get_pending_preview_file_by_window_info (window);
		//printf ("pending: %s\n", pending_preview_filename);

		gboolean use_unoconv = FALSE;
		if (g_find_program_in_path ("unoconv"))
			use_unoconv = TRUE;

		if (is_excel_doc(filename)) {
			//printf ("is excel\n");
			pending_preview_filename = get_html_tmp_name(filename);
			if (!use_unoconv)
				excel2html_by_window_internal (window, pending_preview_filename, doing_filename);
			else
				excel2html_by_window_internal_unoconv (window, pending_preview_filename, doing_filename);
		} else {
			//printf ("is doc or ppt\n");
			pending_preview_filename = get_pdf_tmp_name(filename);
			if (!use_unoconv)
				office2pdf_by_window_internal (window, pending_preview_filename, doing_filename);
			else
				office2pdf_by_window_internal_unoconv (window, pending_preview_filename, doing_filename);
		}		
	} else {
		todo_filename = peony_navigation_window_get_current_previewing_office_file_by_window_info (window);
		//do nothing
		//g_signal_connect (window, "office_trans_ready", G_CALLBACK (trans_next_file), todo_filename);
	}
}

char* get_html_tmp_name (char* filename){
	gchar *doc_path, *doc_name, *tmp_name, *html_dir, *html_path;
	GFile *file;
	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *libreoffice_path;

	libreoffice_path = g_find_program_in_path ("libreoffice");
	if (libreoffice_path == NULL) {
		//printf("libreoffice is not found\n");
		//openoffice_missing_libreoffice (self);
		return NULL;
	}

	//printf("pid: %d\n",getpid());

	file = g_file_new_for_path(filename);
	doc_path = g_file_get_path (file);
	doc_name = g_file_get_basename (file);
	g_object_unref (file);
	tmp_name = g_strrstr (doc_name, ".");
	if (tmp_name){
		*tmp_name = '\0';
	}
	tmp_name = g_strdup_printf ("%s.html", doc_name);
	g_free (doc_name);

	html_dir = g_build_filename (g_get_user_cache_dir (), "peony", NULL);
	html_path = g_build_filename (html_dir, tmp_name, NULL);

	return html_path;
}

char* get_pdf_tmp_name(char* filename){
	gchar *doc_path, *doc_name, *tmp_name, *pdf_dir, *pdf_path;
	GFile *file;
	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *libreoffice_path;

	libreoffice_path = g_find_program_in_path ("libreoffice");
	if (libreoffice_path == NULL) {
		//printf("libreoffice is not found\n");
		//openoffice_missing_libreoffice (self);
		return NULL;
	}

	//printf("pid: %d\n",getpid());

	file = g_file_new_for_path(filename);
	doc_path = g_file_get_path (file);
	doc_name = g_file_get_basename (file);
	g_object_unref (file);
	tmp_name = g_strrstr (doc_name, ".");
	if (tmp_name){
		*tmp_name = '\0';
	}
	tmp_name = g_strdup_printf ("%s.pdf", doc_name);
	g_free (doc_name);

	pdf_dir = g_build_filename (g_get_user_cache_dir (), "peony", NULL);
	pdf_path = g_build_filename (pdf_dir, tmp_name, NULL);

	return pdf_path;
}

char* get_pending_preview_filename (char* filename){
	if (is_excel_doc(filename)) {
		//printf ("is excel\n");
		return get_html_tmp_name(filename);
	} else {
		//printf ("is doc or ppt\n");
		return get_pdf_tmp_name(filename);
	}
}

libreoffice_child_watch_cb2 (GPid pid,
                        gint status,
                        gpointer user_data)
{
	g_spawn_close_pid (pid);
	is_busy = FALSE;
	g_signal_emit_by_name (PEONY_WINDOW_INFO (user_data), "office_trans_ready", NULL);    //office ready cb , if pdf name = global preview file name, show it. else g_remove file.
}

void office2pdf_by_window_internal (PeonyWindowInfo *window, char *pdf_filename, char* office_filename){

	gchar *doc_path, *doc_name, *tmp_name, *pdf_dir, *pdf_path;
	GFile *file;
	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *libreoffice_path;
	
		libreoffice_path = g_find_program_in_path ("libreoffice");
	if (libreoffice_path == NULL) {
		//printf("libreoffice is not found\n");
		return ;
	}

	//printf("pid: %d\n",getpid());

	file = g_file_new_for_path(office_filename);
	doc_path = g_file_get_path (file);
	doc_name = g_file_get_basename (file);
	g_object_unref (file);
	tmp_name = g_strrstr (doc_name, ".");
	if (tmp_name){
		*tmp_name = '\0';
	}
	tmp_name = g_strdup_printf ("%s.pdf", doc_name);
	g_free (doc_name);

	//printf("tmp_name: %s\n",tmp_name);
	pdf_dir = g_build_filename (g_get_user_cache_dir (), "peony", NULL);
	pdf_path = g_build_filename (pdf_dir, tmp_name, NULL);
	g_mkdir_with_parents (pdf_dir, 0700);

	g_free (tmp_name);

	const gchar *libreoffice_argv[] = {
		NULL,
		"--convert-to", "pdf",
		"--outdir", NULL,
		NULL,
		NULL
	};

	libreoffice_argv[0] = libreoffice_path;
	libreoffice_argv[4] = pdf_dir;
	libreoffice_argv[5] = doc_path;

	argv = g_strdupv ((gchar **) libreoffice_argv);

	res = g_spawn_async (NULL, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,
			&pid, &error);

	g_free(pdf_dir);
	g_free(doc_path);
	g_free (libreoffice_path);
	g_strfreev (argv);

	if (!res) {
		g_warning ("Error while spawning libreoffice: %s",
				error->message);
		g_error_free (error);

		return ;
	}

	g_child_watch_add (pid, libreoffice_child_watch_cb2, window);

}

void excel2html_by_window_internal (PeonyWindowInfo *window, char *html_filename, char *excel_filename){

	gchar *doc_path, *doc_name, *tmp_name, *html_dir, *html_path;
	GFile *file;
	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *libreoffice_path;
	
		libreoffice_path = g_find_program_in_path ("libreoffice");
	if (libreoffice_path == NULL) {
		//printf("libreoffice is not found\n");
		return ;
	}

	//printf("pid: %d\n",getpid());

	file = g_file_new_for_path(excel_filename);
	doc_path = g_file_get_path (file);
	doc_name = g_file_get_basename (file);
	g_object_unref (file);
	tmp_name = g_strrstr (doc_name, ".");
	if (tmp_name){
		*tmp_name = '\0';
	}
	tmp_name = g_strdup_printf ("%s.html", doc_name);
	g_free (doc_name);

	//printf("tmp_name: %s\n",tmp_name);
	html_dir = g_build_filename (g_get_user_cache_dir (), "peony", NULL);
	html_path = g_build_filename (html_dir, tmp_name, NULL);
	g_mkdir_with_parents (html_dir, 0700);

	g_free (tmp_name);

	const gchar *libreoffice_argv[] = {
		NULL,
		"--convert-to", "html",
		"--outdir", NULL,
		NULL,
		NULL
	};

	libreoffice_argv[0] = libreoffice_path;
	libreoffice_argv[4] = html_dir;
	libreoffice_argv[5] = doc_path;

	argv = g_strdupv ((gchar **) libreoffice_argv);

	res = g_spawn_async (NULL, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,
			&pid, &error);

	g_free(html_dir);
	g_free(doc_path);
	g_free (libreoffice_path);
	g_strfreev (argv);

	if (!res) {
		g_warning ("Error while spawning libreoffice: %s",
				error->message);
		g_error_free (error);

		return ;
	}

	g_child_watch_add (pid, libreoffice_child_watch_cb2, window);

}

static void
unoconv_child_watch_cb2 (GPid pid,
                        gint status,
                        gpointer user_data)
{
	g_spawn_close_pid (pid);
	is_busy = FALSE;
	g_signal_emit_by_name (PEONY_WINDOW_INFO (user_data), "office_trans_ready", NULL);    //office ready cb , if pdf name = global preview file name, show it. else g_remove file.
}

void office2pdf_by_window_internal_unoconv (PeonyWindowInfo *window, char *pdf_filename, char* office_filename){

	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *unoconv_path;
	char* pdf_file_quoted_path = g_shell_quote (pdf_filename);
	char* office_file_quoted_path = g_shell_quote (office_filename);

	cmd = g_strdup_printf ("unoconv -f pdf -o %s %s", pdf_file_quoted_path, office_file_quoted_path);
	printf("cmd: %s\n",cmd);
	
	res = g_shell_parse_argv (cmd, &argc, &argv, &error);
	//g_free (cmd);
	g_free (pdf_file_quoted_path);
	g_free (office_file_quoted_path);

	if (!res) {
		g_warning ("Error while parsing the unoconv command line: %s",
				error->message);
		g_error_free (error);

		return NULL;
	}

	res = g_spawn_async (NULL, argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,
			&pid, &error);

	g_strfreev (argv);

	if (!res) {
		g_warning ("Error while spawning unoconv: %s",
				error->message);
		g_error_free (error);

		return NULL;
	}

	g_child_watch_add (pid, unoconv_child_watch_cb2, window);

	//g_spawn_close_pid(pid);
}

void excel2html_by_window_internal_unoconv (PeonyWindowInfo *window, char *html_filename, char *excel_filename){

	gboolean res;
	gchar *cmd;

	gint argc;
	GPid pid;
	gchar **argv = NULL;
	GError *error = NULL;
	const gchar *unoconv_path;
	char* html_file_quoted_path = g_shell_quote (html_filename);
	char* excel_file_quoted_path = g_shell_quote (excel_filename);

	cmd = g_strdup_printf ("unoconv -f html -o %s %s", html_file_quoted_path, excel_file_quoted_path);
	printf("cmd: %s\n",cmd);	
	
	res = g_shell_parse_argv (cmd, &argc, &argv, &error);
	//g_free (cmd);
	g_free (html_file_quoted_path);
	g_free (excel_file_quoted_path);

	if (!res) {
		g_warning ("Error while parsing the unoconv command line: %s",
				error->message);
		g_error_free (error);

		return NULL;
	}

	res = g_spawn_async (NULL, argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,
			&pid, &error);

	g_strfreev (argv);

	if (!res) {
		g_warning ("Error while spawning unoconv: %s",
				error->message);
		g_error_free (error);

		return NULL;
	}

	g_child_watch_add (pid, unoconv_child_watch_cb2, window);

}
