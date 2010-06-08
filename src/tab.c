/* This file is part of gPHPEdit, a GNOME2 PHP Editor.
 
   Copyright (C) 2003, 2004, 2005 Andy Jeffries <andy at gphpedit.org>
   Copyright (C) 2009 Anoop John <anoop dot john at zyxware.com>
    
   For more information or to find the latest release, visit our 
   website at http://www.gphpedit.org/
 
   gPHPEdit is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   gPHPEdit is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with gPHPEdit.  If not, see <http://www.gnu.org/licenses/>.
 
   The GNU General Public License is contained in the file COPYING.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "tab.h"
#include "tab_php.h"
#include "tab_css.h"
#include "tab_sql.h"
#include "tab_cxx.h"
#include "tab_perl.h"
#include "tab_cobol.h"
#include "tab_python.h"
#include "main_window_callbacks.h"
#include "grel2abs.h"
#include <gconf/gconf-client.h>
#include "gvfs_utils.h"
//#define DEBUGTAB

#define INFO_FLAGS "standard::display-name,standard::content-type,standard::edit-name,standard::size,access::can-write,access::can-delete,standard::icon,time::modified,time::modified-usec"

GSList *editors;
guint completion_timer_id;
gboolean completion_timer_set;
guint calltip_timer_id;
gboolean calltip_timer_set;
guint gotoline_after_reload = 0;
guint gotoline_after_create_tab = 0;

static void save_point_reached(GtkWidget *w);
static void save_point_left(GtkWidget *w);
static void char_added(GtkWidget *scintilla, guint ch);

void debug_dump_editors(void)
{
  Editor *editor;
  GSList *walk;
  gint editor_number = 0;

  g_print("--------------------------------------------------------------\n");
  g_print("gPHPEdit Debug Output\n\n");

  
  for (walk = editors; walk!=NULL; walk = g_slist_next(walk)) {
    editor = walk->data;
    editor_number++;
    
    g_print("Editor number    :%d\n", editor_number);
    g_print("Scintilla widget :%p\n", editor->scintilla);
    g_print("Scintilla lexer  :%d\n", gtk_scintilla_get_lexer(GTK_SCINTILLA(editor->scintilla)));
    g_print("File mtime       :%lo\n", editor->file_mtime.tv_sec);
    g_print("File shortname   :%s\n", editor->short_filename);
    g_print("File fullname    :%s\n", editor->filename->str);
    if (editor->opened_from) {
      g_print("Opened from      :%s\n", editor->opened_from->str);
    }
    g_print("Saved?           :%d\n", editor->saved);
    g_print("Converted to UTF?:%d\n\n", editor->saved);
  }
  g_print("--------------------------------------------------------------\n");
}

void tab_set_general_scintilla_properties(Editor *editor)
{
  gtk_scintilla_set_backspace_unindents(GTK_SCINTILLA(editor->scintilla), 1);
  gtk_scintilla_autoc_set_choose_single(GTK_SCINTILLA (editor->scintilla), FALSE);
  gtk_scintilla_autoc_set_ignore_case(GTK_SCINTILLA (editor->scintilla), TRUE);
  register_autoc_images(GTK_SCINTILLA (editor->scintilla));
  gtk_scintilla_autoc_set_drop_rest_of_word(GTK_SCINTILLA (editor->scintilla), FALSE);
  gtk_scintilla_set_scroll_width_tracking(GTK_SCINTILLA (editor->scintilla), TRUE);
  gtk_scintilla_set_code_page(GTK_SCINTILLA(editor->scintilla), 65001); // Unicode code page

  gtk_scintilla_set_caret_fore (GTK_SCINTILLA(editor->scintilla), 0);
  gtk_scintilla_set_caret_width (GTK_SCINTILLA(editor->scintilla), 2);
  gtk_scintilla_set_caret_period (GTK_SCINTILLA(editor->scintilla), 250);

  g_signal_connect (G_OBJECT (editor->scintilla), "save_point_reached", G_CALLBACK (save_point_reached), NULL);
  g_signal_connect (G_OBJECT (editor->scintilla), "save_point_left", G_CALLBACK (save_point_left), NULL);
  g_signal_connect (G_OBJECT (editor->scintilla), "macro_record", G_CALLBACK (macro_record), NULL);
  tab_set_configured_scintilla_properties(GTK_SCINTILLA(editor->scintilla), preferences);
  gtk_widget_show (editor->scintilla);
}

void tab_set_configured_scintilla_properties(GtkScintilla *scintilla, Preferences prefs)
{
  gint width;
  width = gtk_scintilla_text_width(scintilla, STYLE_LINENUMBER, "_99999");
  gtk_scintilla_set_margin_width_n(scintilla, 0, width);
  gtk_scintilla_set_margin_width_n (scintilla, 1, 0);
  gtk_scintilla_set_margin_width_n (scintilla, 2, 0);
  gtk_scintilla_set_wrap_mode(scintilla, prefs.line_wrapping);
  if (prefs.line_wrapping) {
    gtk_scintilla_set_h_scroll_bar(scintilla, 0);
  }
  else {
    gtk_scintilla_set_h_scroll_bar(scintilla, 1);
  }
        
  gtk_scintilla_style_set_font (scintilla, STYLE_LINENUMBER, prefs.line_number_font);
  gtk_scintilla_style_set_fore (scintilla, STYLE_LINENUMBER, prefs.line_number_fore);
  gtk_scintilla_style_set_back (scintilla, STYLE_LINENUMBER, prefs.line_number_back);
  gtk_scintilla_style_set_size (scintilla, STYLE_LINENUMBER, prefs.line_number_size);
  /* set font quality */
  gtk_scintilla_set_font_quality(scintilla, prefs.font_quality);

  gtk_scintilla_set_caret_line_visible(scintilla, preferences.higthlightcaretline);
  gtk_scintilla_set_caret_line_back(scintilla, preferences.higthlightcaretline_color);

  gtk_scintilla_set_indentation_guides (scintilla, prefs.show_indentation_guides);
  gtk_scintilla_set_edge_mode (scintilla, prefs.edge_mode);
  gtk_scintilla_set_edge_column (scintilla, prefs.edge_column);
  gtk_scintilla_set_edge_colour (scintilla, prefs.edge_colour);
  gtk_scintilla_set_sel_back(scintilla, 1, prefs.set_sel_back);
  gtk_scintilla_set_caret_fore (scintilla, 0);
  gtk_scintilla_set_caret_width (scintilla, 2);
  gtk_scintilla_set_caret_period (scintilla, 250);

  gtk_scintilla_autoc_set_choose_single (scintilla, FALSE);
  gtk_scintilla_set_use_tabs (scintilla, prefs.use_tabs_instead_spaces);
  gtk_scintilla_set_tab_indents (scintilla, 1);
  gtk_scintilla_set_backspace_unindents (scintilla, 1);
  gtk_scintilla_set_tab_width (scintilla, prefs.indentation_size);
  gtk_scintilla_set_indent (scintilla, prefs.tab_size);

  gtk_scintilla_style_set_font (scintilla, STYLE_DEFAULT, prefs.default_font);
  gtk_scintilla_style_set_size (scintilla, STYLE_DEFAULT, prefs.default_size);
  gtk_scintilla_style_set_italic (scintilla, STYLE_DEFAULT, prefs.default_italic);
  gtk_scintilla_style_set_bold (scintilla, STYLE_DEFAULT, prefs.default_bold);
  gtk_scintilla_style_set_fore (scintilla, STYLE_DEFAULT, prefs.default_fore);
  gtk_scintilla_style_set_back (scintilla, STYLE_DEFAULT, prefs.default_back);

  //annotation styles
  gtk_scintilla_style_set_font (scintilla, STYLE_ANNOTATION_ERROR,  prefs.default_font);
  gtk_scintilla_style_set_size (scintilla,  STYLE_ANNOTATION_ERROR, 8);
  gtk_scintilla_style_set_italic (scintilla,  STYLE_ANNOTATION_ERROR, FALSE);
  gtk_scintilla_style_set_bold (scintilla,  STYLE_ANNOTATION_ERROR, FALSE);
  gtk_scintilla_style_set_fore (scintilla,  STYLE_ANNOTATION_ERROR, 3946645);
  gtk_scintilla_style_set_back (scintilla,  STYLE_ANNOTATION_ERROR, 13355513);

  gtk_scintilla_style_set_font (scintilla, STYLE_ANNOTATION_WARNING,  prefs.default_font);
  gtk_scintilla_style_set_size (scintilla,  STYLE_ANNOTATION_WARNING, 8);
  gtk_scintilla_style_set_italic (scintilla,  STYLE_ANNOTATION_WARNING, FALSE);
  gtk_scintilla_style_set_bold (scintilla,  STYLE_ANNOTATION_WARNING, FALSE);
  gtk_scintilla_style_set_fore (scintilla,  STYLE_ANNOTATION_WARNING, 2859424);
  gtk_scintilla_style_set_back (scintilla,  STYLE_ANNOTATION_WARNING, 10813438);

  //makers margin settings
  gtk_scintilla_set_margin_type_n(GTK_SCINTILLA(scintilla), 1, SC_MARGIN_SYMBOL);
  gtk_scintilla_set_margin_width_n (GTK_SCINTILLA(scintilla), 1, 14);
  gtk_scintilla_set_margin_sensitive_n(GTK_SCINTILLA(scintilla), 1, 1);
  g_signal_connect (G_OBJECT (scintilla), "margin_click", G_CALLBACK (margin_clicked), NULL);
}


static void tab_set_folding(Editor *editor, gint folding)
{
  gint modeventmask;
  
  if (folding) {
    modeventmask = gtk_scintilla_get_mod_event_mask(GTK_SCINTILLA(editor->scintilla));
    gtk_scintilla_set_mod_event_mask(GTK_SCINTILLA(editor->scintilla), modeventmask | SC_MOD_CHANGEFOLD);
    gtk_scintilla_set_fold_flags(GTK_SCINTILLA(editor->scintilla), SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    
    gtk_scintilla_set_margin_type_n(GTK_SCINTILLA(editor->scintilla), 2, SC_MARGIN_SYMBOL);
    gtk_scintilla_set_margin_mask_n(GTK_SCINTILLA(editor->scintilla), 2, SC_MASK_FOLDERS);
    gtk_scintilla_set_margin_sensitive_n(GTK_SCINTILLA(editor->scintilla), 2, 1);
    
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPEN,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPEN,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDER,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDER,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERSUB,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERSUB,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERTAIL,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERTAIL,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEREND,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEREND,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPENMID,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDEROPENMID,0);
    gtk_scintilla_marker_define(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    gtk_scintilla_marker_set_fore(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERMIDTAIL,16777215);
    gtk_scintilla_marker_set_back(GTK_SCINTILLA(editor->scintilla), SC_MARKNUM_FOLDERMIDTAIL,0);
    
    gtk_scintilla_set_margin_width_n (GTK_SCINTILLA(editor->scintilla), 2, 14);
    
/*    //makers margin settings
    gtk_scintilla_set_margin_type_n(GTK_SCINTILLA(main_window.current_editor->scintilla), 1, SC_MARGIN_SYMBOL);
    gtk_scintilla_set_margin_width_n (GTK_SCINTILLA(main_window.current_editor->scintilla), 1, 14);
    gtk_scintilla_set_margin_sensitive_n(GTK_SCINTILLA(main_window.current_editor->scintilla), 1, 1);
*/
    //g_signal_connect (G_OBJECT (editor->scintilla), "fold_clicked", G_CALLBACK (fold_clicked), NULL);
    g_signal_connect (G_OBJECT (editor->scintilla), "modified", G_CALLBACK (handle_modified), NULL);
    //g_signal_connect (G_OBJECT (editor->scintilla), "margin_click", G_CALLBACK (margin_clicked), NULL);
  }
}

static void tab_set_event_handlers(Editor *editor)
{
  g_signal_connect (G_OBJECT (editor->scintilla), "char_added", G_CALLBACK (char_added), NULL);
  g_signal_connect (G_OBJECT (editor->scintilla), "update_ui", G_CALLBACK (update_ui), NULL);
}

gchar *write_buffer = NULL; /*needed for save buffer*/

void tab_file_write (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  Editor *editor = (Editor *)user_data;
  GError *error=NULL;
  if(!g_file_replace_contents_finish ((GFile *)source_object,res,NULL,&error)){
    g_print(_("GIO Error: %s saving file:%s\n"),error->message,editor->filename->str);
    g_free(write_buffer);
    g_error_free(error);
    return;
  }
  g_free(write_buffer);
  gtk_scintilla_set_save_point (GTK_SCINTILLA(editor->scintilla));
  GFileInfo *info;
  info= g_file_query_info ((GFile *)source_object,"time::modified,time::modified-usec",G_FILE_QUERY_INFO_NONE, NULL,&error);
  if (!info){
    g_warning (_("Could not get the file modification time for file: '%s'. GIO error: %s \n"), editor->short_filename,error->message);
    g_get_current_time (&editor->file_mtime); /*set current time*/
  } else {
  /* update modification time */  
    g_file_info_get_modification_time (info,&editor->file_mtime);
    g_object_unref(info);  
  }
  register_file_opened(editor->filename->str);
  classbrowser_update();
  session_save();
}

void tab_file_save_opened(Editor *editor,GFile *file)
{
  gsize text_length;
  GError *error = NULL;
  gchar *converted_text = NULL;
  gsize utf8_size; // was guint
  text_length = gtk_scintilla_get_length(GTK_SCINTILLA(editor->scintilla));
  write_buffer = g_malloc0(text_length+1); // Include terminating null

  if (write_buffer == NULL) {
    g_warning ("%s", _("Cannot allocate write buffer"));
    return;
  }

  gtk_scintilla_get_text(GTK_SCINTILLA(editor->scintilla), text_length+1, write_buffer);
  // If we converted to UTF-8 when loading, convert back to the locale to save
  if (editor->converted_to_utf8) {
    converted_text = g_locale_from_utf8(write_buffer, text_length, NULL, &utf8_size, &error);
    if (error != NULL) {
      g_print(_("UTF-8 Error: %s\n"), error->message);
      g_error_free(error);      
    }
    else {
      #ifdef DEBUGTAB
      g_print("DEBUG: Converted size: %d\n", utf8_size);
      #endif
      g_free(write_buffer);
      write_buffer = converted_text;
      text_length = utf8_size;
    }
  }
  g_file_replace_contents_async (file,write_buffer,text_length,NULL,FALSE,G_FILE_CREATE_NONE,NULL,tab_file_write,editor);
}

void tab_validate_buffer_and_insert(gpointer buffer, Editor *editor)
{
  if (g_utf8_validate(buffer, strlen(buffer), NULL)) {
    #ifdef DEBUGTAB
    g_print("Valid UTF8 according to gnome\n");
    #endif
    gtk_scintilla_add_text(GTK_SCINTILLA (editor->scintilla), strlen(buffer), buffer);
    editor->converted_to_utf8 = FALSE;
  }
  else {
    gchar *converted_text;
    gsize utf8_size;// was guint
    GError *error = NULL;      
    converted_text = g_locale_to_utf8(buffer, strlen(buffer), NULL, &utf8_size, &error);
    if (error != NULL) {
      gssize nchars=strlen(buffer);
      // if locale isn't set
      error=NULL;
      converted_text = g_convert(buffer, nchars, "UTF-8", "ISO-8859-15", NULL, &utf8_size, &error);
      if (error!=NULL){
        g_print(_("gPHPEdit UTF-8 Error: %s\n"), error->message);
        g_error_free(error);
        gtk_scintilla_add_text(GTK_SCINTILLA (editor->scintilla), strlen(buffer), buffer);
      return;
      }
    }
    g_print(_("Converted to UTF-8 size: %d\n"), utf8_size);
    gtk_scintilla_add_text(GTK_SCINTILLA (editor->scintilla), utf8_size, converted_text);
    g_free(converted_text);
    editor->converted_to_utf8 = TRUE;
    }
}

void tab_reset_scintilla_after_open(Editor *editor)
{
  gtk_scintilla_empty_undo_buffer(GTK_SCINTILLA(editor->scintilla));
  gtk_scintilla_set_save_point(GTK_SCINTILLA(editor->scintilla));
  gtk_scintilla_goto_line(GTK_SCINTILLA(editor->scintilla), editor->current_line);
  gtk_scintilla_scroll_caret(GTK_SCINTILLA(editor->scintilla));
  
  gtk_scintilla_grab_focus(GTK_SCINTILLA(editor->scintilla));
}

/*
 * icon_name_from_icon
 *
 *   this function returns the icon name of a Gicon
 *   for the current gtk default icon theme
 */

static gchar *icon_name_from_icon(GIcon *icon) {
  gchar *icon_name=NULL;
  if (icon && G_IS_THEMED_ICON(icon)) {
    GStrv names;
    g_object_get(icon, "names", &names, NULL);
    if (names && names[0]) {
      GtkIconTheme *icon_theme;
      int i;
      icon_theme = gtk_icon_theme_get_default();
      for (i = 0; i < g_strv_length (names); i++) {
        if (gtk_icon_theme_has_icon(icon_theme, names[i])) {
          icon_name = g_strdup(names[i]);
          break;
        }
      }
      g_strfreev (names);
    }
  } else {
    icon_name = g_strdup("application-text");
  }
  if (!icon_name){
    icon_name=g_strdup("application-text");
  }
  return icon_name;
}

void tab_file_opened (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GError *error=NULL;
  Editor *editor = (Editor *)user_data;
  gchar* buffer;
  guint size; 
  if (!g_file_load_contents_finish ((GFile *) source_object,res,&(buffer),&size,NULL,&error)) {
    g_print("Error reading file. Gio error:%s",error->message);
    g_error_free(error);
    return;
  }
  #ifdef DEBUGTAB
  g_print("DEBUG:: Loaded %d bytes\n",editor->file_size);
  #endif
  // Clear scintilla buffer
  gtk_scintilla_clear_all(GTK_SCINTILLA (editor->scintilla));
  #ifdef DEBUGTAB
  g_print("DEBUG:::BUFFER=\n%s\n-------------------------------------------\n", buffer);
  #endif
  tab_validate_buffer_and_insert(buffer, editor);
  tab_reset_scintilla_after_open(editor);
  g_free(buffer);
  if (gotoline_after_reload) {
    goto_line_int(gotoline_after_reload);
    gotoline_after_reload = 0;
  }
  tab_check_php_file(editor);
}

 void tab_load_file(Editor *editor)
{
  GFile *file;
  GFileInfo *info;
  GError *error=NULL;
  
  // Store current position for returning to
  editor->current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(editor->scintilla));
  editor->current_line = gtk_scintilla_line_from_position(GTK_SCINTILLA(editor->scintilla), editor->current_pos);

  // Try getting file size
  file=get_gfile_from_filename(editor->filename->str);
  info= g_file_query_info (file,INFO_FLAGS,G_FILE_QUERY_INFO_NONE, NULL,&error);
  if (!info){
    g_warning (_("Could not get the file info. GIO error: %s \n"), error->message);
    return;
  }
 const char*contenttype= g_file_info_get_content_type (info);
  /*we could open text based types so if it not a text based content don't open and displays error*/
  if (!IS_TEXT(contenttype) && IS_APPLICATION(contenttype)){
    info_dialog (_("gPHPEdit"), _("Sorry, I can open this kind of file.\n"));
    g_print("%s\n",contenttype);
    editor->filename = g_string_new(_("Untitled"));
    editor->short_filename = g_strdup(editor->filename->str);
    editor->is_untitled=TRUE;
    return;
  }
  editor->isreadonly= !g_file_info_get_attribute_boolean (info,"access::can-write");
  editor->contenttype=g_strdup(contenttype);
  GIcon *icon= g_file_info_get_icon (info); /* get Gicon for mimetype*/
  gchar *iconname=icon_name_from_icon(icon);
  editor->file_icon=gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), iconname, GTK_ICON_SIZE_MENU, 0, NULL); // get icon of size menu
  g_free(iconname);
  g_file_info_get_modification_time (info,&editor->file_mtime);
  g_object_unref(info);
  /* Open file*/
  /*it's all ok, read file*/
  g_file_load_contents_async (file,NULL,tab_file_opened,editor);
}

/**
* tab_new_editor
* creates and allocate a new editor must be freed when no longer needed
*/
Editor *tab_new_editor(void)
{
  Editor *editor;
  editor = (Editor *)g_slice_new0(Editor);
  editors = g_slist_append(editors, editor);
  return editor;
}

/**
* str_replace
* replaces tpRp with withc in the string
*/
void str_replace(char *Str, char ToRp, char WithC)
{
  int i = 0;

  while(i < strlen(Str)) {
    if(Str[i] == ToRp) {
      Str[i] = WithC;
    }
    i++;
  }
}


void tab_help_load_file(Editor *editor, GString *filename)
{
  GFile *file;
  GFileInfo *info;
  GError *error=NULL;
  gchar *buffer;
#ifdef PHP_DOC_DIR
  goffset size;
#endif
  gsize nchars;
  file=get_gfile_from_filename(filename->str);
#ifdef PHP_DOC_DIR
  info=g_file_query_info (file,"standard::size,standard::icon",0,NULL,&error);
#else
  info=g_file_query_info (file,"standard::icon",0,NULL,&error);
#endif
  if (!info){
    g_warning (_("Could not get file info for file %s. GIO error: %s \n"), filename->str,error->message);
    return;
  }
#ifdef PHP_DOC_DIR
  size= g_file_info_get_size (info);
#endif
  GIcon *icon= g_file_info_get_icon (info); /* get Gicon for mimetype*/
  gchar *iconname=icon_name_from_icon(icon);
  editor->file_icon=gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), iconname, GTK_ICON_SIZE_MENU, 0, NULL); // get icon of size menu
  g_free(iconname);
  g_object_unref(info);
  if (!g_file_load_contents (file,NULL,&buffer, &nchars,NULL,&error)){
    g_print(_("Error reading file. GIO error:%s\n"),error->message);
    g_free (buffer);
    g_object_unref(file);
    return;
  }
#ifdef PHP_DOC_DIR
  if (size != nchars) g_warning (_("File size and loaded size not matching"));
#endif
  webkit_web_view_load_string (WEBKIT_WEB_VIEW(editor->help_view),buffer,"text/html", "UTF-8", filename->str);

  g_free (buffer);
  g_object_unref(file);
}



GString *tab_help_try_filename(gchar *prefix, gchar *command, gchar *suffix)
{
  GString *long_filename;
        
  long_filename = g_string_new(command);
  long_filename = g_string_prepend(long_filename, prefix);
  if (suffix) {
    long_filename = g_string_append(long_filename, suffix);
  }
  #ifdef DEBUGTAB
  g_print("DEBUG: tab.c:tab_help_try_filename:long_filename->str: %s\n", long_filename->str);
  #endif
  if (filename_file_exist(long_filename->str)){
    return long_filename;
  }
  else {
    g_string_free(long_filename, TRUE);
  }
  long_filename = g_string_new(command);
  str_replace(long_filename->str, '_', '-');
  long_filename = g_string_prepend(long_filename, prefix);
  if (suffix) {
    long_filename = g_string_append(long_filename, suffix);
  }
  #ifdef DEBUGTAB
  g_print("DEBUG: tab.c:tab_help_try_filename:long_filename->str: %s\n", long_filename->str);
  #endif
  if (filename_file_exist(long_filename->str)){
    return long_filename;
  }
  g_string_free(long_filename, TRUE);
  return NULL;
}


GString *tab_help_find_helpfile(gchar *command)
{
  GString *long_filename = NULL;
#ifdef PHP_DOC_DIR
 //FIX: avoid duplicated call
 if (strstr(command,"/usr/")!=NULL){
 return long_filename;
 }
 gchar *temp= NULL;
 temp=g_strdup_printf ("%s/%s",PHP_DOC_DIR,"function.");
 long_filename = tab_help_try_filename(temp, command, ".html");
 g_free(temp);
 if (long_filename)
  return long_filename;
 temp=g_strdup_printf ("%s/%s",PHP_DOC_DIR,"ref.");
 long_filename = tab_help_try_filename(temp, command, ".html");
 g_free(temp);
 if (long_filename)
  return long_filename;
 temp=g_strdup_printf ("%s/",PHP_DOC_DIR);
 long_filename = tab_help_try_filename(temp, command, NULL);
 g_free(temp);
 if (long_filename)
  return long_filename;
 g_print(_("Help for function not found: %s\n"), command);
 return long_filename;
#else
  long_filename = g_string_new("http://www.php.net/manual/en/function.");
  long_filename = g_string_append(long_filename, command);
  long_filename = g_string_append(long_filename, ".php");
  GFile *temp= get_gfile_from_filename(long_filename->str);
  if(g_file_query_exists(temp,NULL)){
  g_object_unref(temp);
  return long_filename;
  }else{
  g_object_unref(temp);
  g_print(_("Help for function not found: %s\n"), command);
  return NULL;
  }
#endif
}
/*
*/
gboolean webkit_link_clicked (WebKitWebView *web_view, WebKitWebFrame *frame, WebKitNetworkRequest *request,
                                                        WebKitWebNavigationAction *navigation_action,
                                                        WebKitWebPolicyDecision   *policy_decision,
                                                        Editor *editor)
{
  gchar *uri= (gchar *)webkit_network_request_get_uri(request);
  if (uri){
    GString *filename;
    //if it's a direction like filename.html#refpoint skips refpoint part
    uri=trunc_on_char(uri, '#');
    if (editor->type==TAB_HELP){ 
    filename=tab_help_find_helpfile(uri);
    } else{
      if (g_str_has_suffix(uri,".htm")){
       return TRUE;
      } else {
      filename=g_string_new(uri);
      }
    }
    if (filename) {
      tab_help_load_file(editor, filename);
      g_string_free(editor->filename, TRUE);

      editor->filename = g_string_new(filename->str);
      if (editor->type==TAB_HELP){
        editor->filename = g_string_prepend(editor->filename, _("Help: "));
        
        //TODO: These strings are not being freed. The app crashes when the free
        //is uncommented stating that there were duplicate free calls.
        //g_free(editor->short_filename);
        //g_free(editor->help_function);
        editor->short_filename = g_strconcat(_("Help: "), uri, NULL);
      } else {
        editor->filename = g_string_prepend(editor->filename, _("Preview: "));
        editor->short_filename = g_strconcat(_("Preview: "), uri, NULL);
      }
      editor->help_function = g_strdup(uri);
      return true;
    }
    }
 return false;
}

static void
notify_title_cb (WebKitWebView* web_view, GParamSpec* pspec, Editor *editor)
{
   char *main_title = g_strdup (webkit_web_view_get_title(web_view));
   if (main_title){
   if (editor->type==TAB_HELP){
     editor->short_filename = g_strconcat(_("Help: "), main_title, NULL);
   } else {
     editor->short_filename = g_strconcat(_("Preview: "), main_title, NULL);
   }
#ifdef PHP_DOC_DIR
   if (editor->help_function) g_free(editor->help_function);
     editor->help_function = main_title;
#endif
     gtk_label_set_text(GTK_LABEL(editor->label), editor->short_filename);
     update_app_title();
   }
}


gboolean tab_create_help(Editor *editor, GString *filename)
{
  GString *caption;
  GString *long_filename = NULL;
  GtkWidget *dialog, *editor_tab;
 
  caption = g_string_new(filename->str);
  caption = g_string_prepend(caption, _("Help: "));

  long_filename = tab_help_find_helpfile(filename->str);
  if (!long_filename) {
    dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
      _("Could not find the required command in the online help"));
    gtk_window_set_transient_for (GTK_WINDOW(dialog),GTK_WINDOW(main_window.window));
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    return FALSE;
  }
  else {
    editor->short_filename = caption->str;
    editor->help_function = g_strdup(filename->str);
    editor->filename = long_filename;
    editor->label = gtk_label_new (caption->str);
    editor->is_untitled = FALSE;
    editor->saved = TRUE;
    editor->contenttype="text/plain";
    gtk_widget_show (editor->label);
    editor->help_view= WEBKIT_WEB_VIEW(webkit_web_view_new ());
    editor->help_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(editor->help_scrolled_window), GTK_WIDGET(editor->help_view));
  
    tab_help_load_file(editor, long_filename);
    #ifdef DEBUGTAB
    g_print("DEBUG::Help file->filename:%s - caption:->%s\n", long_filename, caption->str);
    #endif

    g_signal_connect(G_OBJECT(editor->help_view), "navigation-policy-decision-requested",
       G_CALLBACK(webkit_link_clicked),editor);
    g_signal_connect (G_OBJECT(editor->help_view), "notify::title", G_CALLBACK (notify_title_cb), editor);
    gtk_widget_show_all(editor->help_scrolled_window);
    
    editor_tab = get_close_tab_widget(editor);
    gtk_notebook_append_page (GTK_NOTEBOOK (main_window.notebook_editor), editor->help_scrolled_window, editor_tab);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (main_window.notebook_editor), -1);
  }
  return TRUE;
}

gboolean tab_create_preview(Editor *editor, GString *filename)
{
  GString *caption;
  GtkWidget *editor_tab;
 
  caption = g_string_new(filename->str);
  caption = g_string_prepend(caption, _("Preview: "));

  editor->short_filename = caption->str;
  editor->help_function = g_strdup(filename->str);
  editor->filename = caption;
  editor->label = gtk_label_new (caption->str);
  editor->is_untitled = FALSE;
  editor->saved = TRUE;
  gtk_widget_show (editor->label);
  editor->help_view= WEBKIT_WEB_VIEW(webkit_web_view_new ());
  editor->help_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add(GTK_CONTAINER(editor->help_scrolled_window), GTK_WIDGET(editor->help_view));
  
  tab_help_load_file(editor, filename);

    #ifdef DEBUGTAB
    g_print("DEBUG::Preview file->filename:%s - caption:->%s\n", long_filename, caption->str);
    #endif

  g_signal_connect(G_OBJECT(editor->help_view), "navigation-policy-decision-requested",
       G_CALLBACK(webkit_link_clicked),editor);
  g_signal_connect (G_OBJECT(editor->help_view), "notify::title", G_CALLBACK (notify_title_cb), editor);
  gtk_widget_show_all(editor->help_scrolled_window);
    
  editor_tab = get_close_tab_widget(editor);
  gtk_notebook_append_page (GTK_NOTEBOOK (main_window.notebook_editor), editor->help_scrolled_window, editor_tab);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (main_window.notebook_editor), -1);

  return TRUE;
}

void info_dialog (gchar *title, gchar *message)
{
  GtkWidget *dialog;
  gint button;
  dialog = gtk_message_dialog_new(GTK_WINDOW(main_window.window),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_INFO,GTK_BUTTONS_OK,"%s", message);
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_window_set_transient_for (GTK_WINDOW(dialog),GTK_WINDOW(main_window.window));
  button = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy(dialog);
  /*
   * Run the dialog and wait for the user to select yes or no.
   * If the user closes the window with the window manager, we
   * will get a -4 return value
   */
}

gint yes_no_dialog (gchar *title, gchar *message)
{
  GtkWidget *dialog;
  gint button;
  dialog = gtk_message_dialog_new(GTK_WINDOW(main_window.window),GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_INFO,GTK_BUTTONS_YES_NO,"%s", message);
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_window_set_transient_for (GTK_WINDOW(dialog),GTK_WINDOW(main_window.window));
  button = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy(dialog);
  /*
   * Run the dialog and wait for the user to select yes or no.
   * If the user closes the window with the window manager, we
   * will get a -4 return value
   */
         
  return button;
}

gboolean is_php_file(Editor *editor)
{
  // New style function for configuration of what constitutes a PHP file
  gboolean is_php = FALSE;
  gint i;
  gchar *buffer = NULL;
  gsize text_length;
  gchar **lines;
  gchar *filename;
  
  filename = editor->filename->str;
  is_php=is_php_file_from_filename(filename);

  if (!is_php && editor) { // If it's not recognised as a PHP file, examine the contents for <?php and #!.*php
    
    text_length = gtk_scintilla_get_length(GTK_SCINTILLA(editor->scintilla));
    buffer = g_malloc0(text_length+1); // Include terminating null
    if (buffer == NULL) {
      g_warning ("%s", "Cannot allocate write buffer");
      return is_php;
    }
    gtk_scintilla_get_text(GTK_SCINTILLA(editor->scintilla), text_length+1, buffer);
    lines = g_strsplit(buffer, "\n", 10);
    if (!lines[0])
      return is_php;
    if (lines[0][0] == '#' && lines[0][1] == '!' && strstr(lines[0], "php") != NULL) {
      is_php = TRUE;
    }
    else {
      for (i = 0; lines[i+1] != NULL; i++) {
        if (strstr (lines[i], "<?php") != NULL) {
          is_php = TRUE;
          break;
        }
      }
    }
    g_strfreev(lines);
    g_free(buffer);
  }
  
  return is_php;
}

gboolean is_php_file_from_filename(const gchar *filename)
{
  // New style function for configuration of what constitutes a PHP file
  gchar *file_extension;
  gchar **php_file_extensions;
  gboolean is_php = FALSE;
  gint i;

  file_extension = strrchr(filename, '.');
  if (file_extension) {
    file_extension++;
    
    php_file_extensions = g_strsplit(preferences.php_file_extensions,",",-1);
    
    for (i = 0; php_file_extensions[i] != NULL; i++) {
      if (g_str_has_suffix(filename,php_file_extensions[i])){
        is_php = TRUE;
        break;
      }
    }
        
    g_strfreev(php_file_extensions);
  }
  
  return is_php;
}

gboolean is_css_file(const gchar *filename)
{
  if (g_str_has_suffix(filename,".css"))
      return TRUE;
  return FALSE;
}

gboolean is_perl_file(const gchar *filename)
{
  if (g_str_has_suffix(filename,".pl"))
      return TRUE;
  return FALSE;
}

gboolean is_cobol_file(const gchar *filename)
{
  if (g_str_has_suffix(filename,".cbl") || g_str_has_suffix(filename,".CBL"))
      return TRUE;
  return FALSE;
}

gboolean is_python_file(const gchar *filename)
{
  if (g_str_has_suffix(filename,".py"))
      return TRUE;
  return FALSE;
}

gboolean is_cxx_file(const gchar *filename)
{
  if (g_str_has_suffix(filename,".cxx") || g_str_has_suffix(filename,".c") || g_str_has_suffix(filename,".h"))
      return TRUE;
  return FALSE;
}

gboolean is_sql_file(const gchar *filename)
{
if (g_str_has_suffix(filename,".sql"))
      return TRUE;
  return FALSE;
}

void set_editor_to_php(Editor *editor)
{
  tab_php_set_lexer(editor);
  gtk_scintilla_set_word_chars((GtkScintilla *)editor->scintilla, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$");
  editor->type = TAB_PHP;
  tab_set_folding(editor, TRUE);
}

void set_editor_to_css(Editor *editor)
{
  tab_css_set_lexer(editor);
  gtk_scintilla_set_word_chars((GtkScintilla *)editor->scintilla, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");
  editor->type = TAB_CSS;
  tab_set_folding(editor, TRUE);
}

void set_editor_to_sql(Editor *editor)
{
  tab_sql_set_lexer(editor);
  editor->type = TAB_SQL;
}

void set_editor_to_cxx(Editor *editor)
{
  tab_cxx_set_lexer(editor);
  editor->type = TAB_CXX;
  tab_set_folding(editor, TRUE);
}

void set_editor_to_perl(Editor *editor)
{
  tab_perl_set_lexer(editor);
  editor->type = TAB_PERL;
}

void set_editor_to_cobol(Editor *editor)
{
  tab_cobol_set_lexer(editor);
  editor->type = TAB_COBOL;
}

void set_editor_to_python(Editor *editor)
{
  tab_python_set_lexer(editor);
  editor->type = TAB_PYTHON;
}

void tab_check_php_file(Editor *editor)
{
  if (is_php_file(editor)) {
    set_editor_to_php(editor);
  }
}


void tab_check_css_file(Editor *editor)
{
  if (is_css_file(editor->filename->str)) {
    set_editor_to_css(editor);
  }
}

void tab_check_perl_file(Editor *editor)
{
  if (is_perl_file(editor->filename->str)) {
    set_editor_to_perl(editor);
  }
}

void tab_check_cobol_file(Editor *editor)
{
  if (is_cobol_file(editor->filename->str)) {
    set_editor_to_cobol(editor);
  }
}
void tab_check_python_file(Editor *editor)
{
  if (is_python_file(editor->filename->str)) {
    set_editor_to_python(editor);
  }
}

void tab_check_cxx_file(Editor *editor)
{
  if (is_cxx_file(editor->filename->str)) {
    set_editor_to_cxx(editor);
  }
}

void tab_check_sql_file(Editor *editor)
{
  if (is_sql_file(editor->filename->str)) {
    set_editor_to_sql(editor);
  }
}


void register_file_opened(gchar *filename)
{
  gchar *full_filename=filename_get_uri(filename);
  main_window_add_to_reopen_menu(full_filename);
  g_free(full_filename);
  gchar *folder = filename_parent_uri(filename);
  GConfClient *config;
  config=gconf_client_get_default ();
  if (folder){
    gconf_client_set_string (config,"/gPHPEdit/general/last_opened_folder",folder,NULL);
    g_free(folder);
  }
}

gboolean switch_to_file_or_open(gchar *filename, gint line_number)
{
  Editor *editor;
  GSList *walk;
  GString *tmp_filename;
  // need to check if filename is local before adding to the listen
  filename = g_strdup(filename);
  for (walk = editors; walk!=NULL; walk = g_slist_next(walk)) {
    editor = walk->data;
    if (strcmp(editor->filename->str, filename)==0) {
      gtk_notebook_set_current_page( GTK_NOTEBOOK(main_window.notebook_editor), gtk_notebook_page_num(GTK_NOTEBOOK(main_window.notebook_editor),editor->scintilla));
      main_window.current_editor = editor;
      gotoline_after_reload = line_number;
      //on_reload1_activate(NULL);
      goto_line_int(line_number);
      return TRUE;
    }
  }
  gotoline_after_create_tab = line_number;
  tmp_filename = g_string_new(filename);
  tab_create_new(TAB_FILE, tmp_filename);
  g_string_free(tmp_filename, TRUE);
  register_file_opened(filename);
  session_save();
  g_free(filename);
  return TRUE;
}

gchar *recordar;
void openfile_mount(GObject *source_object,GAsyncResult *res,gpointer user_data) {
  GError *error=NULL;
  if (g_file_mount_enclosing_volume_finish((GFile *)source_object,res,&error)) {
    /* open again */
    switch_to_file_or_open(recordar, 0);
  } else {
    g_print(_("Error mounting volume. GIO error:%s\n"),error->message);
  }
}

/* Create a new tab and return TRUE if a tab was created */
gboolean tab_create_new(gint type, GString *filename)
{
  Editor *editor;
  GString *dialog_message;
  GtkWidget *editor_tab;
  gchar abs_buffer[2048];
  gchar *abs_path = NULL;
  gchar *cwd;
  gboolean result;
  gboolean file_created = FALSE;
  GFile *file;
  #ifdef DEBUGTAB
    g_print("DEBUG: tab.c:tab_create_new:filename->str: %s\n", filename->str);
  #endif
  if (filename != NULL) {
    if (strstr(filename->str, ":")==NULL) {
      cwd = g_get_current_dir();
      abs_path = g_strdup(g_rel2abs (filename->str, cwd, abs_buffer, 2048));
      g_free(cwd);
    }
    else {
      abs_path = g_strdup(filename->str);
    }
    file=get_gfile_from_filename(abs_path);
    if (!uri_is_local_or_http(abs_path)){
      GError *error2=NULL;
      GMount *ex= g_file_find_enclosing_mount (file,NULL,&error2);
      if (!ex){
        if (error2->code== G_IO_ERROR_NOT_MOUNTED){
          g_print(_("Error filesystem not mounted.\nMounting filesystem, this will take a few seconds...\n"));
          GMountOperation *gmo;
          gmo= gtk_mount_operation_new(GTK_WINDOW(main_window.window));
          recordar=g_strdup(filename->str);
          g_file_mount_enclosing_volume (file, G_MOUNT_MOUNT_NONE,gmo,NULL, openfile_mount, recordar);
          g_error_free(error2);
          error2=NULL;        
          return FALSE;
        }
        g_print(_("Error opening file GIO error:%s\nfile:%s"),error2->message,abs_path);
        error2=NULL;
        return FALSE;
      }
    }
    if(!g_file_query_exists (file,NULL) && type!=TAB_HELP && type!=TAB_PREVIEW) {
      dialog_message = g_string_new("");
      g_string_printf(dialog_message, _("The file %s was not found.\n\nWould you like to create it as an empty document?"), filename->str);
      result = yes_no_dialog(_("File not found"), dialog_message->str);
      g_string_free(dialog_message, TRUE);
      g_object_unref(file);
      if (result != -8){
        return FALSE;
      }
      file_created = TRUE;
    }
    g_object_unref(file); /* not sure */
  }
  // When a new tab request is processed if the only current tab is an untitled
  // and unmodified tab then close it 
  // close_saved_empty_Untitled();
  editor = tab_new_editor();
  editor->type = type;
  if (editor->type == TAB_HELP) {

    if (!tab_create_help(editor, filename)) {
    // Couldn't find the help file, don't keep the editor
    editors = g_slist_remove(editors, editor);
    } else {
      editor->saved = TRUE;
    }

  } else if (editor->type== TAB_PREVIEW) {
      if (!tab_create_preview(editor, filename)) {
      // Couldn't find the help file, don't keep the editor
      editors = g_slist_remove(editors, editor);
      }
      else {
      editor->saved = TRUE;
      }
  } else {
    editor->type = TAB_FILE;
    editor->scintilla = gtk_scintilla_new();
    tab_set_general_scintilla_properties(editor);
    editor->label = gtk_label_new (_("Untitled"));
    gtk_widget_show (editor->label);
                
    if (abs_path != NULL) {
      editor->filename = g_string_new(abs_path);
      editor->short_filename = filename_get_basename (editor->filename->str);
      if (!file_created) tab_load_file(editor);
      //tab_check_php_file(editor);
      tab_check_css_file(editor);
      tab_check_cxx_file(editor);
      tab_check_perl_file(editor);
      tab_check_cobol_file(editor);
      tab_check_python_file(editor);
      tab_check_sql_file(editor);
//      classbrowser_update();
      editor->is_untitled=FALSE;
    } else {
      editor->filename = g_string_new(_("Untitled"));
      editor->short_filename = g_strdup(editor->filename->str);
      if (main_window.current_editor) {
        gchar *tfilename=filename_parent_uri(main_window.current_editor->filename->str);
        editor->opened_from = g_string_new(tfilename);
        g_free(tfilename);
      }
      editor->is_untitled=TRUE;
      /* set default text icon */
      editor->file_icon= gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "text-plain", GTK_ICON_SIZE_MENU, 0, NULL); // get icon of size menu
    }
    // Hmmm, I had the same error as the following comment.  A reshuffle here and upgrading GtkScintilla2 to 0.1.0 seems to have fixed it
    if (!gtk_widget_get_visible(editor->scintilla)) gtk_widget_show (editor->scintilla);
    
    editor_tab = get_close_tab_widget(editor);
    gtk_notebook_append_page (GTK_NOTEBOOK (main_window.notebook_editor), editor->scintilla, editor_tab);
    gtk_scintilla_set_save_point(GTK_SCINTILLA(editor->scintilla));
    tab_set_event_handlers(editor);
    
    /* Possible problem on the next line, one user reports: 
      assertion `GTK_WIDGET_ANCHORED (widget) || GTK_IS_INVISIBLE (widget)' failed */
    gtk_notebook_set_current_page (GTK_NOTEBOOK (main_window.notebook_editor), -1);
    gtk_scintilla_goto_pos(GTK_SCINTILLA(editor->scintilla), 0);
    gtk_scintilla_grab_focus(GTK_SCINTILLA(editor->scintilla));
    main_window.current_editor = editor;

    if (gotoline_after_create_tab) {
      goto_line_int(gotoline_after_create_tab);
      gotoline_after_create_tab = 0;
    }
    editor->saved=TRUE;
  }
  update_app_title();
  classbrowser_update();
  g_free(abs_path);

  return TRUE;
}

GtkWidget *get_close_tab_widget(Editor *editor) {
  GtkWidget *hbox, *image, *close_button;
  GtkRcStyle *rcstyle;

  hbox = gtk_hbox_new(FALSE, 0);
  image = gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
  gtk_misc_set_padding(GTK_MISC(image), 0, 0);
  gtk_container_set_border_width(GTK_CONTAINER(hbox), 0);
  close_button = gtk_button_new();
  gtk_widget_set_tooltip_text(close_button, "Close Tab");

  gtk_button_set_image(GTK_BUTTON(close_button), image);
  gtk_button_set_relief(GTK_BUTTON(close_button), GTK_RELIEF_NONE);
  gtk_button_set_focus_on_click(GTK_BUTTON(close_button), FALSE);

  rcstyle = gtk_rc_style_new ();
  rcstyle->xthickness = rcstyle->ythickness = 0;
  gtk_widget_modify_style (close_button, rcstyle);
  g_object_unref(rcstyle);
  g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(on_tab_close_activate), editor);
  g_signal_connect(G_OBJECT(hbox), "style-set", G_CALLBACK(on_tab_close_set_style), close_button);
  /* load file icon */
  GtkWidget *icon= gtk_image_new_from_pixbuf (editor->file_icon);
  gtk_widget_show (icon);
  gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(hbox), editor->label, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(hbox), close_button, FALSE, FALSE, 0);
  gtk_widget_show(editor->label);
  gtk_widget_show(image);
  gtk_widget_show(close_button);
  gtk_widget_show(hbox);
  return hbox;
}

Editor *editor_find_from_scintilla(GtkWidget *scintilla)
{
  GSList *walk;
  Editor *editor;

  for (walk = editors; walk != NULL; walk = g_slist_next (walk)) {
    editor = walk->data;
    if (editor->scintilla == scintilla) {
      return walk->data;
    }
  }

  return NULL;
}


Editor *editor_find_from_help(void *help)
{
  GSList *walk;
  Editor *editor;

  for (walk = editors; walk != NULL; walk = g_slist_next (walk)) {
    editor = walk->data;
    if ((void *)(editor->help_scrolled_window == help)) {
      return walk->data;
    }
  }
  return NULL;
}


static void save_point_reached(GtkWidget *scintilla)
{
  Editor *editor;

  editor = editor_find_from_scintilla(scintilla);
  if (!editor) return;
  if (editor->short_filename != NULL) {
    gtk_label_set_text(GTK_LABEL (editor->label), editor->short_filename);
    editor->saved=TRUE;
    update_app_title();
  }
}

static void save_point_left(GtkWidget *scintilla)
{
  Editor *editor;
  gchar *caption;
  editor = editor_find_from_scintilla(scintilla);
  if (!editor) return;
  if (editor->short_filename != NULL) {
    caption= g_strdup_printf("*%s",editor->short_filename);
    gtk_label_set_text(GTK_LABEL (editor->label), caption);
    g_free(caption);
    editor->saved=FALSE;
    update_app_title();
  }
}

// All the folding functions are converted from QScintilla, released under the GPLv2 by
// Riverbank Computing Limited <info@riverbankcomputing.co.uk> and Copyright (c) 2003 by them.
void fold_clicked(GtkWidget *scintilla, guint lineClick, guint bstate)
{
  gint levelClick;
   
  levelClick = gtk_scintilla_get_fold_level(GTK_SCINTILLA(scintilla), lineClick);

  if (levelClick & SC_FOLDLEVELHEADERFLAG)
  {
    if (bstate & SCMOD_SHIFT) {
      // Ensure all children are visible.
      gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), lineClick, 1);
      fold_expand(scintilla, lineClick, TRUE, TRUE, 100, levelClick);
    }
    else if (bstate & SCMOD_CTRL) {
      if (gtk_scintilla_get_fold_expanded(GTK_SCINTILLA(scintilla), lineClick)) {
        // Contract this line and all its children.
        gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), lineClick, 0);
        fold_expand(scintilla, lineClick, FALSE, TRUE, 0, levelClick);
      }
      else {
        // Expand this line and all its children.
        gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), lineClick, 1);
        fold_expand(scintilla, lineClick, TRUE, TRUE, 100, levelClick);
      }
    }
    else {
      // Toggle this line.
      gtk_scintilla_toggle_fold(GTK_SCINTILLA(scintilla), lineClick);
    }
  }
}

// All the folding functions are converted from QScintilla, released under the GPLv2 by
// Riverbank Computing Limited <info@riverbankcomputing.co.uk> and Copyright (c) 2003 by them.
void fold_expand(GtkWidget *scintilla, gint line, gboolean doExpand, gboolean force, gint visLevels, gint level)
{
  gint lineMaxSubord;
  gint levelLine;
  
  lineMaxSubord = gtk_scintilla_get_last_child(GTK_SCINTILLA(scintilla), line, level & SC_FOLDLEVELNUMBERMASK);

  while (line <= lineMaxSubord) {
    line++;
    if (force) {
      if (visLevels > 0) {
        gtk_scintilla_show_lines(GTK_SCINTILLA(scintilla), line, line);
      }
      else {
        gtk_scintilla_hide_lines(GTK_SCINTILLA(scintilla), line, line);
      }
    }
    else if (doExpand) {
      gtk_scintilla_show_lines(GTK_SCINTILLA(scintilla), line, line);
    }

    levelLine = level;

    if (levelLine == -1) {
      levelLine = gtk_scintilla_get_fold_level(GTK_SCINTILLA(scintilla), line);
    }

    if (levelLine & SC_FOLDLEVELHEADERFLAG) {
      if (force) {
        if (visLevels > 1) {
          gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), line, 1);
        }
        else {
          gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), line, 0);
        }

        fold_expand(scintilla, line, doExpand, force, visLevels - 1, 0); // Added last 0 param - AJ
      }
      else if (doExpand) {
        if (!gtk_scintilla_get_fold_expanded(GTK_SCINTILLA(scintilla), line)) {
          gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), line, 1);
        }

        fold_expand(scintilla, line, TRUE, force, visLevels - 1, 0); // Added last 0 param - AJ
      }
      else {
        fold_expand(scintilla, line, FALSE, force, visLevels - 1, 0); // Added last 0 param - AJ
      }
    }
    else {
      line++;
    }
  }
}

// All the folding functions are converted from QScintilla, released under the GPLv2 by
// Riverbank Computing Limited <info@riverbankcomputing.co.uk> and Copyright (c) 2003 by them.
void fold_changed(GtkWidget *scintilla, int line,int levelNow,int levelPrev)
{
  if (levelNow & SC_FOLDLEVELHEADERFLAG) {
    if (!(levelPrev & SC_FOLDLEVELHEADERFLAG))
      gtk_scintilla_set_fold_expanded(GTK_SCINTILLA(scintilla), line, 1);
        }
  else if (levelPrev & SC_FOLDLEVELHEADERFLAG) {
    if (!gtk_scintilla_get_fold_expanded(GTK_SCINTILLA(scintilla), line)) {
      // Removing the fold from one that has been contracted
      // so should expand.  Otherwise lines are left
      // invisible with no way to make them visible.
      fold_expand(scintilla, line, TRUE, FALSE, 0, levelPrev);
    }
  }
}

// All the folding functions are converted from QScintilla, released under the GPLv2 by
// Riverbank Computing Limited <info@riverbankcomputing.co.uk> and Copyright (c) 2003 by them.
void handle_modified(GtkWidget *scintilla, gint pos,gint mtype,gchar *text,gint len,
           gint added,gint line,gint foldNow,gint foldPrev)
{
  if (preferences.show_folding && (mtype & SC_MOD_CHANGEFOLD)) {
    fold_changed(scintilla, line, foldNow, foldPrev);
  }
}

void margin_clicked (GtkWidget *scintilla, gint modifiers, gint position, gint margin)
{
  if(margin!=1){
    gint line;
    line = gtk_scintilla_line_from_position(GTK_SCINTILLA(scintilla), position);
    if (preferences.show_folding && margin == 2) {
      fold_clicked(scintilla, line, modifiers);
    }
  }else{
    gint line;
    line = gtk_scintilla_line_from_position(GTK_SCINTILLA(scintilla), position);
    mod_marker(line);
  }
}


char *macro_message_to_string(gint message)
{
  switch (message) {
    case (2170) : return "REPLACE SELECTION";
    case (2177) : return "CLIPBOARD CUT";
    case (2178) : return "CLIPBOARD COPY";
    case (2179) : return "CLIPBOARD PASTE";
    case (2180) : return "CLEAR";
    case (2300) : return "LINE DOWN";
    case (2301) : return "LINE DOWN EXTEND";
    case (2302) : return "LINE UP";
    case (2303) : return "LINE UP EXTEND";
    case (2304) : return "CHAR LEFT";
    case (2305) : return "CHAR LEFT EXTEND";
    case (2306) : return "CHAR RIGHT";
    case (2307) : return "CHAR RIGHT EXTEND";
    case (2308) : return "WORD LEFT";
    case (2309) : return "WORD LEFT EXTEND";
    case (2310) : return "WORD RIGHT";
    case (2311) : return "WORD RIGHT EXTEND";
    case (2312) : return "HOME";
    case (2313) : return "HOME EXTEND";
    case (2314) : return "LINE END";
    case (2315) : return "LINE END EXTEND";
    case (2316) : return "DOCUMENT START";
    case (2317) : return "DOCUMENT START EXTEND";
    case (2318) : return "DOCUMENT END";
    case (2319) : return "DOCUMENT END EXTEND";
    case (2320) : return "PAGE UP";
    case (2321) : return "PAGE UP EXTEND";
    case (2322) : return "PAGE DOWN";
    case (2323) : return "PAGE DOWN EXTEND";
    default:
      return "-- UNKNOWN --";
    /*case (2324) : gtk_scintilla_edit_toggle_overtype(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2325) : gtk_scintilla_cancel(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2326) : gtk_scintilla_delete_back(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2327) : gtk_scintilla_tab(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2328) : gtk_scintilla_back_tab(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2329) : gtk_scintilla_new_line(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2330) : gtk_scintilla_form_feed(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2331) : gtk_scintilla_v_c_home(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2332) : gtk_scintilla_v_c_home_extend(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2333) : gtk_scintilla_zoom_in(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2334) : gtk_scintilla_zoom_out(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2335) : gtk_scintilla_del_word_left(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2336) : gtk_scintilla_del_word_right(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2337) : gtk_scintilla_line_cut(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2338) : gtk_scintilla_line_delete(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2339) : gtk_scintilla_line_transpose(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2340) : gtk_scintilla_lower_case(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2341) : gtk_scintilla_upper_case(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2342) : gtk_scintilla_line_scroll_down(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2343) : gtk_scintilla_line_scroll_up(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2344) : gtk_scintilla_delete_back_not_line(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2345) : gtk_scintilla_home_display(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2346) : gtk_scintilla_home_display_extend(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2347) : gtk_scintilla_line_end_display(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    case (2348) : gtk_scintilla_line_end_display_extend(GTK_SCINTILLA(main_window.current_editor->scintilla)); break;
    default:
      g_print("Unhandle keyboard macro function %d, please report to macro@gphpedit.org\n", event->message);*/
  }
}

void macro_record (GtkWidget *scintilla, gint message, gulong wparam, glong lparam)
{
  Editor *editor;
  MacroEvent *event;
  
  editor = editor_find_from_scintilla(scintilla);
  if (editor->is_macro_recording) {
    event = g_new0(MacroEvent, 1);
    event->message = message;
    event->wparam = wparam;
    // Special handling for text inserting, duplicate inserted string
    if (event->message == 2170 && editor->is_pasting) {
      event->message = 2179;
    }
    else if (event->message == 2170) {
      event->lparam = (glong) g_strdup((gchar *)lparam);
    }
    else {
      event->lparam = lparam;
    }
    #ifdef DEBUGTAB
    g_print("DEBUG: tab.c:macro_record:Message: %d (%s)\n", event->message, macro_message_to_string(event->message));
    #endif
    editor->keyboard_macro_list = g_slist_append(editor->keyboard_macro_list, event);
  }
}

void keyboard_macro_empty_old(Editor *editor)
{
  GSList *current;
  MacroEvent *event;

  for (current = editor->keyboard_macro_list; current; current = g_slist_next(current)) {
    event = current->data;
    if (event->message == 2170) {
      // Special handling for text inserting, free inserted string
      g_free((gchar *)event->lparam);
    }
    g_free(event);
  }
  g_slist_free(editor->keyboard_macro_list);
  editor->keyboard_macro_list = NULL;
}

static void indent_line(GtkWidget *scintilla, gint line, gint indent)
{
  gint selStart;
  gint selEnd;
  gint posBefore;
  gint posAfter;
  gint posDifference;

  selStart = gtk_scintilla_get_selection_start(GTK_SCINTILLA(scintilla));
  selEnd = gtk_scintilla_get_selection_end(GTK_SCINTILLA(scintilla));
  posBefore = gtk_scintilla_get_line_indentation(GTK_SCINTILLA(scintilla), line);
  gtk_scintilla_set_line_indentation(GTK_SCINTILLA(scintilla), line, indent);
  posAfter = gtk_scintilla_get_line_indentation(GTK_SCINTILLA(scintilla), line);
  posDifference =  posAfter - posBefore;

  if (posAfter > posBefore) {
    // Move selection on
    if (selStart >= posBefore) {
      selStart += posDifference;
    }
    if (selEnd >= posBefore) {
      selEnd += posDifference;
    }
  }
  else if (posAfter < posBefore) {
    // Move selection back
    if (selStart >= posAfter) {
      if (selStart >= posBefore)
        selStart += posDifference;
      else
        selStart = posAfter;
    }
    if (selEnd >= posAfter) {
      if (selEnd >= posBefore)
        selEnd += posDifference;
      else
        selEnd = posAfter;
    }
  }
  gtk_scintilla_set_selection_start(GTK_SCINTILLA(scintilla), selStart);
  gtk_scintilla_set_selection_end(GTK_SCINTILLA(scintilla), selEnd);
}

gboolean auto_complete_callback(gpointer data)
{
  gint wordStart;
  gint wordEnd;
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    autocomplete_word(main_window.current_editor->scintilla, wordStart, wordEnd);
  }
  completion_timer_set=FALSE;
  return FALSE;
}


gboolean css_auto_complete_callback(gpointer data)
{
  gint wordStart;
  gint wordEnd;
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    css_autocomplete_word(main_window.current_editor->scintilla, wordStart, wordEnd);
  }
  completion_timer_set=FALSE;
  return FALSE;
}

gboolean cobol_auto_complete_callback(gpointer data)
{
  gint wordStart;
  gint wordEnd;
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    cobol_autocomplete_word(main_window.current_editor->scintilla, wordStart, wordEnd);
  }
  completion_timer_set=FALSE;
  return FALSE;
}

gboolean sql_auto_complete_callback(gpointer data)
{
  gint wordStart;
  gint wordEnd;
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    sql_autocomplete_word(main_window.current_editor->scintilla, wordStart, wordEnd);
  }
  completion_timer_set=FALSE;
  return FALSE;
}


gboolean auto_memberfunc_complete_callback(gpointer data)
{
  gint wordStart;
  gint wordEnd;
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(main_window.current_editor->scintilla), current_pos-1, TRUE);
    autocomplete_member_function(main_window.current_editor->scintilla, wordStart, wordEnd);
  }
  completion_timer_set=FALSE;
  return FALSE;
}

gboolean calltip_callback(gpointer data)
{
  gint current_pos;

  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(main_window.current_editor->scintilla));

  if (current_pos == (gint)data) {
    show_call_tip(main_window.current_editor->scintilla, main_window.current_editor->type,current_pos);
  }
  calltip_timer_set=FALSE;
  return FALSE;

}


void update_ui(GtkWidget *scintilla)
{
  // ----------------------------------------------------
  // This code is based on that found in SciTE
  // Converted by AJ 2004-03-04
  // ----------------------------------------------------
  
  int current_brace_pos = -1;
  int matching_brace_pos = -1;
  int current_brace_column = -1;
  int matching_brace_column = -1;
  int current_line;
  int current_pos;
  char character_before = '\0';
  char character_after = '\0';
  //char style_before = '\0';
  
  if (gtk_scintilla_call_tip_active(GTK_SCINTILLA(scintilla))) {
    gtk_scintilla_call_tip_cancel(GTK_SCINTILLA(scintilla));
  }
    
  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(scintilla));
  current_line = gtk_scintilla_line_from_position(GTK_SCINTILLA(scintilla), gtk_scintilla_get_current_pos(GTK_SCINTILLA(scintilla)));
  
  //Check if the character before the cursor is a brace.
  if (current_pos > 0) {
    character_before = gtk_scintilla_get_char_at(GTK_SCINTILLA(scintilla), current_pos - 1);
    // style_before = gtk_scintilla_get_style_at(GTK_SCINTILLA(scintilla), current_pos - 1);
    if (character_before && strchr("[](){}", character_before)) {
      current_brace_pos = current_pos - 1;
    }
  }
  //If the character before the cursor is not a brace then
  //check if the character at the cursor is a brace.
  if (current_brace_pos < 0) {
    character_after = gtk_scintilla_get_char_at(GTK_SCINTILLA(scintilla), current_pos);
    // style_before = gtk_scintilla_get_style_at(GTK_SCINTILLA(scintilla), current_pos);
    if (character_after && strchr("[](){}", character_after)) {
      current_brace_pos = current_pos;
    }
  }
  //find the matching brace  
  if (current_brace_pos>=0) {
    matching_brace_pos = gtk_scintilla_brace_match(GTK_SCINTILLA(scintilla), current_brace_pos);
  }
  
  
  // If no brace has been found or we aren't editing PHP code
  if ((current_brace_pos==-1)) {
    gtk_scintilla_brace_bad_light(GTK_SCINTILLA(scintilla), -1);// Remove any existing highlight
    return;
  }
  
  // A brace has been found, but there isn't a matching one ...
  if (current_brace_pos!=1 && matching_brace_pos==-1) {
    // ... therefore send the bad_list message so it highlights the brace in red
    gtk_scintilla_brace_bad_light(GTK_SCINTILLA(scintilla), current_brace_pos);
  }
  else {
    // a brace has been found and a matching one, so highlight them both
    gtk_scintilla_brace_highlight(GTK_SCINTILLA(scintilla), current_brace_pos, matching_brace_pos);
    
    // and highlight the indentation marker
    current_brace_column = gtk_scintilla_get_column(GTK_SCINTILLA(scintilla), current_brace_pos);
    matching_brace_column = gtk_scintilla_get_column(GTK_SCINTILLA(scintilla), matching_brace_pos);
    
    gtk_scintilla_set_highlight_guide(GTK_SCINTILLA(scintilla), MIN(current_brace_column, matching_brace_pos));
  }
  
}

static void char_added(GtkWidget *scintilla, guint ch)
{
  gint current_pos;
  gint wordStart;
  gint wordEnd;
  gint current_word_length;
  gint current_line;
  gint previous_line;
  gint previous_line_indentation;
  gchar *ac_buffer = NULL;
  gint ac_length;
  gchar *member_function_buffer = NULL;
  gint member_function_length;
  gint previous_line_end;
  gchar *previous_char_buffer;
  gint previous_char_buffer_length;
  guint style;
  gint type;

  type = main_window.current_editor->type;
  #ifdef DEBUGTAB
  g_print("DEBUG:::char added:%d\n",ch);
  #endif
//  if (type == TAB_HELP || type == TAB_PREVIEW || type==TAB_FILE) return;
  if (type == TAB_HELP || type == TAB_PREVIEW) return;
  if ((type != TAB_PHP) && (ch=='\r'|| ch=='\n' || ch=='\t'))return;
  current_pos = gtk_scintilla_get_current_pos(GTK_SCINTILLA(scintilla));
  current_line = gtk_scintilla_line_from_position(GTK_SCINTILLA(scintilla), current_pos);
  wordStart = gtk_scintilla_word_start_position(GTK_SCINTILLA(scintilla), current_pos-1, TRUE);
  wordEnd = gtk_scintilla_word_end_position(GTK_SCINTILLA(scintilla), current_pos-1, TRUE);
  current_word_length = wordEnd - wordStart;
  style = gtk_scintilla_get_style_at(GTK_SCINTILLA(scintilla), current_pos);
  if (preferences.auto_complete_braces){
    if (ch=='{'){
      gtk_scintilla_insert_text(GTK_SCINTILLA(scintilla), current_pos,"}");
      gtk_scintilla_goto_pos(GTK_SCINTILLA(scintilla), current_pos);
    } else if (ch=='('){
      gtk_scintilla_insert_text(GTK_SCINTILLA(scintilla), current_pos,")");
      gtk_scintilla_goto_pos(GTK_SCINTILLA(scintilla), current_pos);
   } else if (ch=='['){
     gtk_scintilla_insert_text(GTK_SCINTILLA(scintilla), current_pos,"]");
     gtk_scintilla_goto_pos(GTK_SCINTILLA(scintilla), current_pos);
    }
  }
  if (gtk_scintilla_autoc_active(GTK_SCINTILLA(scintilla))==1) {
    style = 0; // Hack to get around the drop-down not showing in comments, but if it's been forced...  
  }

  switch(type) {
    case(TAB_PHP):   
      gtk_scintilla_autoc_set_fill_ups(GTK_SCINTILLA(scintilla), "( .");
      if ((style != SCE_HPHP_SIMPLESTRING) && (style != SCE_HPHP_HSTRING) && (style != SCE_HPHP_COMMENTLINE) && (style !=SCE_HPHP_COMMENT)) {
      switch(ch) {
          case ('\r'):
          case ('\n'):
        if (current_line>0) {
        previous_line = current_line-1;
        previous_line_indentation = gtk_scintilla_get_line_indentation(GTK_SCINTILLA(scintilla), previous_line);

        previous_line_end = gtk_scintilla_get_line_end_position(GTK_SCINTILLA(scintilla), previous_line);
        previous_char_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), previous_line_end-1, previous_line_end, &previous_char_buffer_length);
        if (*previous_char_buffer=='{') {
            previous_line_indentation+=preferences.indentation_size;
        }
        g_free(previous_char_buffer);
        indent_line(scintilla, current_line, previous_line_indentation);
        #ifdef DEBUGTAB
        g_print("DEBUG: tab.c:char_added:previous_line=%d, previous_indent=%d\n", previous_line, previous_line_indentation);
        #endif
        gint pos;
        if (preferences.use_tabs_instead_spaces) {
        pos= gtk_scintilla_position_from_line(GTK_SCINTILLA(scintilla), current_line)+(previous_line_indentation/gtk_scintilla_get_tab_width(GTK_SCINTILLA(scintilla)));
        }
        else {
        pos=gtk_scintilla_position_from_line(GTK_SCINTILLA(scintilla), current_line)+(previous_line_indentation);
        }
        gtk_scintilla_goto_pos(GTK_SCINTILLA(scintilla), pos);
        }
          case (')'):
        if (gtk_scintilla_call_tip_active(GTK_SCINTILLA(scintilla))) {
        gtk_scintilla_call_tip_cancel(GTK_SCINTILLA(scintilla));
        }
        break; /*exit nothing todo */
          case ('('):
        if ((gtk_scintilla_get_line_state(GTK_SCINTILLA(scintilla), current_line))==274 &&
        (calltip_timer_set==FALSE)) {
          calltip_timer_id = g_timeout_add(preferences.calltip_delay, calltip_callback, (gpointer) current_pos);
          calltip_timer_set=TRUE;
        }
        break;
          default:      
        member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordEnd-1, wordEnd +1, &member_function_length);
        if (strcmp(member_function_buffer, "->")==0 || strcmp(member_function_buffer, "::")==0) {
          /*search back for a '$' in that line */
          gint initial_pos=gtk_scintilla_position_from_line(GTK_SCINTILLA(scintilla), current_line);
          gint line_size;
          gchar *line_text= gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), initial_pos, wordStart-1, &line_size);
            if (!check_php_variable_before(line_text))return;
            if (completion_timer_set==FALSE) {
              completion_timer_id = g_timeout_add(preferences.auto_complete_delay, auto_memberfunc_complete_callback, (gpointer) current_pos);
              completion_timer_set=TRUE;
            }
        }
        g_free(member_function_buffer);
        member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart, wordEnd, &member_function_length);
        if (g_str_has_prefix(member_function_buffer,"$") || g_str_has_prefix(member_function_buffer,"__")) {
            autocomplete_php_variables(scintilla, wordStart, wordEnd);
        } else if (strcmp(member_function_buffer,"instanceof")==0 || strcmp(member_function_buffer,"is_subclass_of")==0) {
        gtk_scintilla_insert_text(GTK_SCINTILLA(scintilla), current_pos," ");
        gtk_scintilla_goto_pos(GTK_SCINTILLA(scintilla), current_pos +1);
        autocomplete_php_classes(scintilla, wordStart, wordEnd);
        } else if ((current_word_length>=3) && ( (gtk_scintilla_get_line_state(GTK_SCINTILLA(scintilla), current_line)==274))) {
        // check to see if they've typed <?php and if so do nothing
        if (wordStart>1) {
          ac_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart-2, wordEnd, &ac_length);
          if (strcmp(ac_buffer,"<?php")==0) {
            g_free(ac_buffer);
            break;
          }
          g_free(ac_buffer);
        }
        if (gtk_scintilla_autoc_active(GTK_SCINTILLA(scintilla))==1) {
          autocomplete_word(scintilla, wordStart, wordEnd);
        } else {
          completion_timer_id = g_timeout_add(preferences.auto_complete_delay, auto_complete_callback, (gpointer) current_pos);
        }
        }
        g_free(member_function_buffer);
        }
        break;
      case(TAB_CSS):
        gtk_scintilla_autoc_set_fill_ups(GTK_SCINTILLA(scintilla), ":");
      switch(ch) {
          case (';'):
        if (gtk_scintilla_call_tip_active(GTK_SCINTILLA(scintilla))) {
        gtk_scintilla_call_tip_cancel(GTK_SCINTILLA(scintilla));
        }
        break; /*exit nothing to do*/
          case (':'):
        if ((calltip_timer_set==FALSE)) {
          calltip_timer_id = g_timeout_add(preferences.calltip_delay, calltip_callback, (gpointer) current_pos);
          calltip_timer_set=TRUE;
        }
        break;
          default:  
        member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart-2, wordStart, &member_function_length);
        if(current_word_length>=3){
          css_autocomplete_word(scintilla, wordStart, wordEnd);
        }
        if (gtk_scintilla_autoc_active(GTK_SCINTILLA(scintilla))!=1) {
            if (completion_timer_set==FALSE) {
              completion_timer_id = g_timeout_add(preferences.auto_complete_delay, auto_memberfunc_complete_callback, (gpointer) current_pos);
              completion_timer_set=TRUE;
            }
          }  
        g_free(member_function_buffer);
        }
        break;
      case(TAB_COBOL):
        member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart-2, wordStart, &member_function_length);
        if(current_word_length>=3){
          cobol_autocomplete_word(scintilla, wordStart, wordEnd);
        }
        if (gtk_scintilla_autoc_active(GTK_SCINTILLA(scintilla))!=1) {
            if (completion_timer_set==FALSE) {
              completion_timer_id = g_timeout_add(preferences.auto_complete_delay, auto_memberfunc_complete_callback, (gpointer) current_pos);
              completion_timer_set=TRUE;
            }
          }  
        g_free(member_function_buffer);
        break;
      case(TAB_SQL): 
        member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart-2, wordStart, &member_function_length);
        if(current_word_length>=3){
          sql_autocomplete_word(scintilla, wordStart, wordEnd);
        }
        if (gtk_scintilla_autoc_active(GTK_SCINTILLA(scintilla))!=1) {
            if (completion_timer_set==FALSE) {
              completion_timer_id = g_timeout_add(preferences.auto_complete_delay, auto_memberfunc_complete_callback, (gpointer) current_pos);
              completion_timer_set=TRUE;
            }
          }  
        g_free(member_function_buffer);
        break;
      }
      // Drop down for HTML here (line_state = 272)
     default:
            member_function_buffer = gtk_scintilla_get_text_range (GTK_SCINTILLA(scintilla), wordStart-2, wordEnd, &member_function_length);
            /* if we type <?php then we are in a php file so force php syntax mode */
            if (strcmp(member_function_buffer,"<?php")==0) set_editor_to_php(main_window.current_editor);
//          g_print("buffer:%s",member_function_buffer);
            g_free(member_function_buffer);
            break;
    }
}

gboolean editor_is_local(Editor *editor)
{
  gboolean result=FALSE;
  gchar *filename;
  
  filename = filename_get_path(editor->filename->str);
  if (filename){
  g_free(filename);
  result=TRUE;
  }
  #ifdef DEBUGTAB
  g_print("DEBUG::: %s filename:%s",(result)?"TRUE - local!!!":"FALSE - not local!!!",editor->filename->str);
  #endif

  return result;
}

gboolean uri_is_local_or_http(gchar *uri)
{
  gchar *filename;

  filename = uri;
  if (g_str_has_prefix(filename, "file://")){
    return TRUE;
  }
  if (g_str_has_prefix(filename, "http://")){
    return TRUE;
  }
  if (g_str_has_prefix(filename, "https://")){
    return TRUE;
  }
  if (g_str_has_prefix(filename, "/")){
    return TRUE;
  }
  #ifdef DEBUGTAB
  g_print("DEBUG:: %s - not local!!!",uri);
  #endif
  return FALSE;
}
