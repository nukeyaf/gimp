/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * action-search-dialog.c
 * Copyright (C) 2012-2013 Srihari Sriraman
 *                         Suhas V
 *                         Vidyashree K
 *                         Zeeshan Ali Ansari
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <ctype.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include <gdk/gdkkeysyms.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "dialogs-types.h"

#include "config/gimpguiconfig.h"

#include "core/gimp.h"

#include "widgets/gimpaction.h"
#include "widgets/gimpaction-history.h"
#include "widgets/gimpdialogfactory.h"
#include "widgets/gimphelp-ids.h"
#include "widgets/gimpuimanager.h"

#include "action-search-dialog.h"

#include "gimp-intl.h"


enum
{
  COLUMN_ICON,
  COLUMN_MARKUP,
  COLUMN_TOOLTIP,
  COLUMN_ACTION,
  COLUMN_SENSITIVE,
  COLUMN_SECTION,
  N_COL
};

typedef struct
{
  GtkWidget     *dialog;

  Gimp          *gimp;
  GtkWidget     *keyword_entry;
  GtkWidget     *results_list;
  GtkWidget     *list_view;

  gint           window_height;
} SearchDialog;


static gboolean     action_search_entry_key_pressed        (GtkWidget         *widget,
                                                            GdkEventKey       *event,
                                                            SearchDialog      *private);
static void         action_search_entry_key_released       (GtkWidget         *widget,
                                                            GdkEventKey       *event,
                                                            SearchDialog      *private);
static gboolean     action_search_list_key_pressed         (GtkWidget         *widget,
                                                            GdkEventKey       *pKey,
                                                            SearchDialog      *private);
static void         action_search_list_row_activated       (GtkTreeView       *treeview,
                                                            GtkTreePath       *path,
                                                            GtkTreeViewColumn *col,
                                                            SearchDialog      *private);
static gboolean     action_search_view_accel_find_func     (GtkAccelKey       *key,
                                                            GClosure          *closure,
                                                            gpointer           data);
static gchar *      action_search_find_accel_label         (GtkAction         *action);
static void         action_search_add_to_results_list      (GtkAction         *action,
                                                            SearchDialog      *private,
                                                            gint               section);
static void         action_search_run_selected             (SearchDialog      *private);
static void         action_search_history_and_actions      (const gchar       *keyword,
                                                            SearchDialog      *private);
static gboolean     action_search_match_keyword            (GtkAction         *action,
                                                            const gchar*       keyword,
                                                            gint              *section,
                                                            Gimp              *gimp);

static void         action_search_hide                     (SearchDialog      *private);

static gboolean     action_search_window_configured        (GtkWindow         *window,
                                                            GdkEvent          *event,
                                                            SearchDialog      *private);
static void         action_search_setup_results_list       (GtkWidget        **results_list,
                                                            GtkWidget        **list_view);
static void         search_dialog_free                     (SearchDialog      *private);


/* Public Functions */

GtkWidget *
action_search_dialog_create (Gimp *gimp)
{
  static SearchDialog *private = NULL;
  GdkScreen           *screen  = gdk_screen_get_default ();

  if (! private)
    {
      GtkWidget *action_search_dialog = gtk_window_new (GTK_WINDOW_TOPLEVEL);
      GdkScreen *screen               = gdk_screen_get_default ();
      GdkWindow *gdk_parent           = gdk_screen_get_active_window (screen);
      gpointer   parent_widget;
      GtkWindow *parent;
      GtkWidget *main_vbox;

      private = g_slice_new0 (SearchDialog);
      g_object_weak_ref (G_OBJECT (action_search_dialog),
                         (GWeakNotify) search_dialog_free, private);

      private->dialog = action_search_dialog;
      private->gimp   = gimp;

      gtk_window_set_role (GTK_WINDOW (action_search_dialog),
                           "gimp-action-search-dialog");
      gtk_window_set_title (GTK_WINDOW (action_search_dialog),
                            _("Search Actions"));
      gtk_window_set_modal (GTK_WINDOW (action_search_dialog), TRUE);
      /* The user data for a GdkWindow is its associated widget. */
      gdk_window_get_user_data (gdk_parent, &parent_widget);
      parent = GTK_WINDOW(gtk_widget_get_toplevel (GTK_WIDGET (parent_widget)));
      /* NOTE: gtk_window_set_keep_above() would be easier but would render
       * the search dialog above any windows, even non-GIMP related, which
       * some early testers found annoying.
       * Setting it transient forces it above a single parent instead. */
      gtk_window_set_transient_for (GTK_WINDOW (action_search_dialog), parent);

      main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
      gtk_container_add (GTK_CONTAINER (action_search_dialog), main_vbox);
      gtk_widget_show (main_vbox);

      private->keyword_entry = gtk_entry_new ();
      gtk_entry_set_icon_from_icon_name (GTK_ENTRY (private->keyword_entry),
                                         GTK_ENTRY_ICON_PRIMARY, "edit-find");
      gtk_box_pack_start (GTK_BOX (main_vbox), private->keyword_entry,
                          FALSE, FALSE, 0);
      gtk_widget_show (private->keyword_entry);

      action_search_setup_results_list (&private->results_list,
                                        &private->list_view);
      gtk_box_pack_start (GTK_BOX (main_vbox), private->list_view, TRUE, TRUE, 0);

      gtk_widget_set_events (private->dialog,
                             GDK_KEY_RELEASE_MASK  |
                             GDK_KEY_PRESS_MASK    |
                             GDK_BUTTON_PRESS_MASK |
                             GDK_SCROLL_MASK);

      g_signal_connect (private->keyword_entry, "key-release-event",
                        G_CALLBACK (action_search_entry_key_released),
                        private);
      g_signal_connect (private->keyword_entry, "key-press-event",
                        G_CALLBACK (action_search_entry_key_pressed),
                        private);

      g_signal_connect (private->results_list, "key-press-event",
                        G_CALLBACK (action_search_list_key_pressed),
                        private);
      g_signal_connect (private->results_list, "row-activated",
                        G_CALLBACK (action_search_list_row_activated),
                        private);

      g_signal_connect (private->dialog, "configure-event",
                        G_CALLBACK (action_search_window_configured),
                        private);
      g_signal_connect (private->dialog, "delete-event",
                        G_CALLBACK (gtk_widget_hide_on_delete),
                        NULL);
    }

  private->window_height = gdk_screen_get_height (screen) / 2;

  return private->dialog;
}

/* Private Functions */
static gboolean
action_search_entry_key_pressed (GtkWidget    *widget,
                                 GdkEventKey  *event,
                                 SearchDialog *private)
{
  gboolean event_processed = FALSE;

  if (event->keyval == GDK_KEY_Down)
    {
      GtkTreeView *tree_view = GTK_TREE_VIEW (private->results_list);

      /* When hitting the down key while editing, select directly the
       * second item, since the first could have run directly with
       * Enter. */
      gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view),
                                      gtk_tree_path_new_from_string ("1"));
      gtk_widget_grab_focus (GTK_WIDGET (private->results_list));
      event_processed = TRUE;
    }

  return event_processed;
}

static void
action_search_entry_key_released (GtkWidget    *widget,
                                  GdkEventKey  *event,
                                  SearchDialog *private)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (private->results_list);
  gchar       *entry_text;
  gint         width;

  gtk_window_get_size (GTK_WINDOW (private->dialog), &width, NULL);

  entry_text = g_strstrip (gtk_editable_get_chars (GTK_EDITABLE (widget), 0, -1));

  switch (event->keyval)
    {
    case GDK_KEY_Escape:
      action_search_hide (private);
      return;

    case GDK_KEY_Return:
      action_search_run_selected (private);
      return;
    }

  if (strcmp (entry_text, "") != 0)
    {
      gtk_window_resize (GTK_WINDOW (private->dialog),
                         width, private->window_height);
      gtk_list_store_clear (GTK_LIST_STORE (gtk_tree_view_get_model (tree_view)));
      gtk_widget_show_all (private->list_view);
      action_search_history_and_actions (entry_text, private);
      gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view),
                                      gtk_tree_path_new_from_string ("0"));
    }
  else if (strcmp (entry_text, "") == 0 && (event->keyval == GDK_KEY_Down))
    {
      gtk_window_resize (GTK_WINDOW (private->dialog),
                         width, private->window_height);
      gtk_list_store_clear (GTK_LIST_STORE (gtk_tree_view_get_model (tree_view)));
      gtk_widget_show_all (private->list_view);
      action_search_history_and_actions (NULL, private);
      gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view),
                                      gtk_tree_path_new_from_string ("0"));
    }
  else
    {
      GtkTreeSelection *selection;
      GtkTreeModel     *model;
      GtkTreeIter       iter;

      selection = gtk_tree_view_get_selection (tree_view);
      gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

      if (gtk_tree_selection_get_selected (selection, &model, &iter))
        {
          GtkTreePath *path;

          path = gtk_tree_model_get_path (model, &iter);
          gtk_tree_selection_unselect_path (selection, path);

          gtk_tree_path_free (path);
        }

      gtk_widget_hide (private->list_view);
      gtk_window_resize (GTK_WINDOW (private->dialog), width, 1);
    }

  g_free (entry_text);
}

static gboolean
action_search_list_key_pressed (GtkWidget    *widget,
                                GdkEventKey  *kevent,
                                SearchDialog *private)
{
  switch (kevent->keyval)
    {
    case GDK_KEY_Return:
      {
        action_search_run_selected (private);
        break;
      }
    case GDK_KEY_Escape:
      {
        action_search_hide (private);
        return TRUE;
      }
    case GDK_KEY_Up:
      {
        gboolean          event_processed = FALSE;
        GtkTreeSelection *selection;
        GtkTreeModel     *model;
        GtkTreeIter       iter;

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->results_list));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

        if (gtk_tree_selection_get_selected (selection, &model, &iter))
          {
            GtkTreePath *path = gtk_tree_model_get_path (model, &iter);

            if (strcmp (gtk_tree_path_to_string (path), "0") == 0)
              {
                gint start_pos;
                gint end_pos;

                gtk_editable_get_selection_bounds (GTK_EDITABLE (private->keyword_entry),
                                                   &start_pos, &end_pos);
                gtk_widget_grab_focus ((GTK_WIDGET (private->keyword_entry)));
                gtk_editable_select_region (GTK_EDITABLE (private->keyword_entry),
                                            start_pos, end_pos);

                event_processed = TRUE;
              }

            gtk_tree_path_free (path);
          }

        return event_processed;
      }
    case GDK_KEY_Down:
      {
        return FALSE;
      }
    default:
      {
        gint start_pos;
        gint end_pos;

        gtk_editable_get_selection_bounds (GTK_EDITABLE (private->keyword_entry),
                                           &start_pos, &end_pos);
        gtk_widget_grab_focus ((GTK_WIDGET (private->keyword_entry)));
        gtk_editable_select_region (GTK_EDITABLE (private->keyword_entry),
                                    start_pos, end_pos);
        gtk_widget_event (GTK_WIDGET (private->keyword_entry),
                          (GdkEvent *) kevent);
      }
    }

  return FALSE;
}

static void
action_search_list_row_activated (GtkTreeView        *treeview,
                                  GtkTreePath        *path,
                                  GtkTreeViewColumn  *col,
                                  SearchDialog       *private)
{
  action_search_run_selected (private);
}

static gboolean
action_search_view_accel_find_func (GtkAccelKey *key,
                                    GClosure    *closure,
                                    gpointer     data)
{
  return (GClosure *) data == closure;
}

static gchar *
action_search_find_accel_label (GtkAction *action)
{
  guint            accel_key     = 0;
  GdkModifierType  accel_mask    = 0;
  GClosure        *accel_closure = NULL;
  gchar           *accel_string;
  GtkAccelGroup   *accel_group;
  GimpUIManager   *manager;

  manager       = gimp_ui_managers_from_name ("<Image>")->data;
  accel_group   = gtk_ui_manager_get_accel_group (GTK_UI_MANAGER (manager));
  accel_closure = gtk_action_get_accel_closure (action);

  if (accel_closure)
   {
     GtkAccelKey *key;

     key = gtk_accel_group_find (accel_group,
                                 action_search_view_accel_find_func,
                                 accel_closure);
     if (key            &&
         key->accel_key &&
         key->accel_flags & GTK_ACCEL_VISIBLE)
       {
         accel_key  = key->accel_key;
         accel_mask = key->accel_mods;
       }
   }

  accel_string = gtk_accelerator_get_label (accel_key, accel_mask);

  if (strcmp (g_strstrip (accel_string), "") == 0)
    {
      /* The value returned by gtk_accelerator_get_label() must be freed after use. */
      g_free (accel_string);
      accel_string = NULL;
    }

  return accel_string;
}

static void
action_search_add_to_results_list (GtkAction    *action,
                                   SearchDialog *private,
                                   gint          section)
{
  GtkTreeIter   iter;
  GtkTreeIter   next_section;
  GtkListStore *store;
  GtkTreeModel *model;
  gchar        *markup;
  gchar        *action_name;
  gchar        *label;
  gchar        *escaped_label = NULL;
  const gchar  *icon_name;
  gchar        *accel_string;
  gchar        *escaped_accel = NULL;
  gboolean      has_shortcut = FALSE;
  const gchar  *tooltip;
  gchar        *escaped_tooltip = NULL;
  gboolean      has_tooltip  = FALSE;

  label = g_strstrip (gimp_strip_uline (gtk_action_get_label (action)));

  if (! label || strlen (label) == 0)
    {
      g_free (label);
      return;
    }

  escaped_label = g_markup_escape_text (label, -1);

  if (GTK_IS_TOGGLE_ACTION (action))
    {
      if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
        icon_name = "gtk-ok";
      else
        icon_name = "gtk-no";
    }
  else
    {
      icon_name = gtk_action_get_icon_name (action);
    }

  accel_string = action_search_find_accel_label (action);
  if (accel_string != NULL)
    {
      escaped_accel = g_markup_escape_text (accel_string, -1);
      has_shortcut = TRUE;
    }

  tooltip = gtk_action_get_tooltip (action);
  if (tooltip != NULL)
    {
      escaped_tooltip = g_markup_escape_text (tooltip, -1);
      has_tooltip = TRUE;
    }

  markup = g_strdup_printf ("%s<small>%s%s%s<span weight='light'>%s</span></small>",
                            escaped_label,
                            has_shortcut ? " | " : "",
                            has_shortcut ? escaped_accel : "",
                            has_tooltip ? "\n" : "",
                            has_tooltip ? escaped_tooltip : "");

  action_name = g_markup_escape_text (gtk_action_get_name (action), -1);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (private->results_list));
  store = GTK_LIST_STORE (model);
  if (gtk_tree_model_get_iter_first (model, &next_section))
    {
      while (TRUE)
        {
          gint iter_section;

          gtk_tree_model_get (model, &next_section,
                              COLUMN_SECTION, &iter_section, -1);
          if (iter_section > section)
            {
              gtk_list_store_insert_before (store, &iter, &next_section);
              break;
            }
          else if (! gtk_tree_model_iter_next (model, &next_section))
            {
              gtk_list_store_append (store, &iter);
              break;
            }
        }
    }
  else
    {
      gtk_list_store_append (store, &iter);
    }

  gtk_list_store_set (store, &iter,
                      COLUMN_ICON,      icon_name,
                      COLUMN_MARKUP,    markup,
                      COLUMN_TOOLTIP,   action_name,
                      COLUMN_ACTION,    action,
                      COLUMN_SECTION,   section,
                      COLUMN_SENSITIVE, gtk_action_is_sensitive (action),
                      -1);

  g_free (accel_string);
  g_free (markup);
  g_free (action_name);
  g_free (label);
  g_free (escaped_accel);
  g_free (escaped_label);
  g_free (escaped_tooltip);
}

static void
action_search_run_selected (SearchDialog *private)
{
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  GtkTreeIter       iter;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (private->results_list));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      GtkAction *action;

      gtk_tree_model_get (model, &iter, COLUMN_ACTION, &action, -1);

      if (gtk_action_is_sensitive (action))
        {
          action_search_hide (private);
          gtk_action_activate (action);
        }

      g_object_unref (action);
    }

  return;
}

static void
action_search_history_and_actions (const gchar  *keyword,
                                   SearchDialog *private)
{
  GimpUIManager *manager;
  GList         *list;
  GList         *history_actions = NULL;

  manager = gimp_ui_managers_from_name ("<Image>")->data;

  if (g_strcmp0 (keyword, "") == 0)
    return;

  history_actions = gimp_action_history_search (private->gimp,
                                                action_search_match_keyword,
                                                keyword);

  /* First put on top of the list any matching action of user history. */
  for (list = history_actions; list; list = g_list_next (list))
    {
      action_search_add_to_results_list (GTK_ACTION (list->data), private, 0);
    }

  /* Now check other actions. */
  for (list = gtk_ui_manager_get_action_groups (GTK_UI_MANAGER (manager));
       list;
       list = g_list_next (list))
    {
      GList           *list2;
      GimpActionGroup *group   = list->data;
      GList           *actions = NULL;

      actions = gtk_action_group_list_actions (GTK_ACTION_GROUP (group));
      actions = g_list_sort (actions, (GCompareFunc) gimp_action_name_compare);

      for (list2 = actions; list2; list2 = g_list_next (list2))
        {
          const gchar *name;
          GtkAction   *action       = list2->data;
          gboolean     is_redundant = FALSE;
          gint         section;

          name = gtk_action_get_name (action);

          /* The action search dialog don't show any non-historized
           * action, with the exception of "plug-in-repeat/reshow"
           * actions.
           * Logging them is meaningless (they may mean a different
           * actual action each time), but they are still interesting
           * as a search result.
           */
          if (gimp_action_history_excluded_action (name) &&
              g_strcmp0 (name, "plug-in-repeat") != 0    &&
              g_strcmp0 (name, "plug-in-reshow") != 0)
            continue;

          if (! gtk_action_is_sensitive (action) &&
              ! GIMP_GUI_CONFIG (private->gimp->config)->search_show_unavailable)
            continue;

          if (action_search_match_keyword (action, keyword, &section, private->gimp))
            {
              GList *list3;

              /* A matching action. Check if we have not already added
               * it as an history action.
               */
              for (list3 = history_actions; list3; list3 = g_list_next (list3))
                {
                  if (strcmp (gtk_action_get_name (GTK_ACTION (list3->data)),
                              name) == 0)
                    {
                      is_redundant = TRUE;
                      break;
                    }
                }

              if (! is_redundant)
                {
                  action_search_add_to_results_list (action, private, section);
                }
            }
        }

      g_list_free (actions);
   }

  g_list_free_full (history_actions, (GDestroyNotify) g_object_unref);
}

static gboolean
action_search_match_keyword (GtkAction   *action,
                             const gchar *keyword,
                             gint        *section,
                             Gimp        *gimp)
{
  gboolean   matched = FALSE;
  gchar    **key_tokens;
  gchar    **label_tokens;
  gchar    **label_alternates = NULL;
  gchar    *tmp;

  if (keyword == NULL)
    {
      /* As a special exception, a NULL keyword means any action
       * matches.
       */
      if (section)
        {
          *section = 0;
        }
      return TRUE;
    }

  key_tokens   = g_str_tokenize_and_fold (keyword, gimp->config->language, NULL);
  tmp          = gimp_strip_uline (gtk_action_get_label (action));
  label_tokens = g_str_tokenize_and_fold (tmp, gimp->config->language, &label_alternates);
  g_free (tmp);

  /* If keyword is two characters, then match them with first letters
   * of first and second word in the labels.  For instance 'gb' will
   * list 'Gaussian Blur...'
   */
  if (g_strv_length (key_tokens) == 1 && g_utf8_strlen (key_tokens[0], -1) == 2)
    {
      gunichar c1 = g_utf8_get_char (key_tokens[0]);
      gunichar c2 = g_utf8_get_char (g_utf8_find_next_char (key_tokens[0], NULL));

      if ((g_strv_length   (label_tokens) > 1          &&
           g_utf8_get_char (label_tokens[0]) == c1     &&
           g_utf8_get_char (label_tokens[1]) == c2)    ||
          (g_strv_length   (label_alternates) > 1      &&
           g_utf8_get_char (label_alternates[0]) == c1 &&
           g_utf8_get_char (label_alternates[1]) == c2))
        {
          matched = TRUE;
          if (section)
            {
              *section = 1;
            }
        }
    }

  if (! matched && g_strv_length (label_tokens) > 0)
    {
      gint     previous_matched = -1;
      gboolean match_start;
      gboolean match_ordered;
      gint     i;

      matched       = TRUE;
      match_start   = TRUE;
      match_ordered = TRUE;
      for (i = 0; key_tokens[i] != NULL; i++)
        {
          gint j;
          for (j = 0; label_tokens[j] != NULL; j++)
            {
              if (g_str_has_prefix (label_tokens[j], key_tokens[i]))
                {
                  goto one_matched;
                }
            }
          for (j = 0; label_alternates[j] != NULL; j++)
            {
              if (g_str_has_prefix (label_alternates[j], key_tokens[i]))
                {
                  goto one_matched;
                }
            }
          matched = FALSE;
one_matched:
          if (previous_matched > j)
            match_ordered = FALSE;
          previous_matched = j;

          if (i != j)
            match_start = FALSE;

          continue;
        }

      if (matched && section)
        {
          /* If the key is the label start, this is a nicer match.
           * Then if key tokens are found in the same order in the label.
           * Finally we show at the end if the key tokens are found with a different order. */
          *section = match_ordered ? (match_start ? 1 : 2) : 3;
        }
    }

  if (! matched && g_utf8_strlen (key_tokens[0], -1) > 2 &&
      gtk_action_get_tooltip (action) != NULL)
    {
      gchar    **tooltip_tokens;
      gchar    **tooltip_alternates = NULL;
      gboolean   mixed_match;
      gint       i;

      tooltip_tokens = g_str_tokenize_and_fold (gtk_action_get_tooltip (action),
                                                gimp->config->language, &tooltip_alternates);

      if (g_strv_length (tooltip_tokens) > 0)
        {
          matched     = TRUE;
          mixed_match = FALSE;

          for (i = 0; key_tokens[i] != NULL; i++)
            {
              gint j;
              for (j = 0; tooltip_tokens[j] != NULL; j++)
                {
                  if (g_str_has_prefix (tooltip_tokens[j], key_tokens[i]))
                    {
                      goto one_tooltip_matched;
                    }
                }
              for (j = 0; tooltip_alternates[j] != NULL; j++)
                {
                  if (g_str_has_prefix (tooltip_alternates[j], key_tokens[i]))
                    {
                      goto one_tooltip_matched;
                    }
                }
              for (j = 0; label_tokens[j] != NULL; j++)
                {
                  if (g_str_has_prefix (label_tokens[j], key_tokens[i]))
                    {
                      mixed_match = TRUE;
                      goto one_tooltip_matched;
                    }
                }
              for (j = 0; label_alternates[j] != NULL; j++)
                {
                  if (g_str_has_prefix (label_alternates[j], key_tokens[i]))
                    {
                      mixed_match = TRUE;
                      goto one_tooltip_matched;
                    }
                }
              matched = FALSE;
one_tooltip_matched:
              continue;
            }
          if (matched && section)
            {
              /* Matching the tooltip is section 4. We don't go looking
               * for start of string or token order for tooltip match.
               * But if the match is mixed on tooltip and label (there are
               * no match for *only* label or *only* tooltip), this is
               * section 5. */
              *section = mixed_match ? 5 : 4;
            }
        }
      g_strfreev (tooltip_tokens);
      g_strfreev (tooltip_alternates);
    }

  g_strfreev (key_tokens);
  g_strfreev (label_tokens);
  g_strfreev (label_alternates);

  return matched;
}

static void
action_search_hide (SearchDialog *private)
{
  if (GTK_IS_WIDGET (private->dialog))
    {
      gint width;

      gtk_window_get_size (GTK_WINDOW (private->dialog), &width, NULL);

      gtk_entry_set_text (GTK_ENTRY (private->keyword_entry), "");
      gtk_widget_hide (private->list_view);
      gtk_window_resize (GTK_WINDOW (private->dialog), width, 1);
      gimp_dialog_factory_hide_dialog (private->dialog);
    }
}

static gboolean
action_search_window_configured (GtkWindow    *window,
                                 GdkEvent     *event,
                                 SearchDialog *private)
{
  if (gtk_widget_get_visible (GTK_WIDGET (window)) &&
      gtk_widget_get_visible (private->list_view))
    {
      gtk_window_get_size (GTK_WINDOW (private->dialog),
                           NULL, &private->window_height);
    }

  return FALSE;
}

static void
action_search_setup_results_list (GtkWidget **results_list,
                                  GtkWidget **list_view)
{
  gint                wid1 = 100;
  GtkListStore       *store;
  GtkCellRenderer    *cell;
  GtkTreeViewColumn  *column;

  *list_view = GTK_WIDGET (gtk_scrolled_window_new (NULL, NULL));
  store = gtk_list_store_new (N_COL,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              G_TYPE_STRING,
                              GTK_TYPE_ACTION,
                              G_TYPE_BOOLEAN,
                              G_TYPE_INT);
  *results_list = GTK_WIDGET (gtk_tree_view_new_with_model (GTK_TREE_MODEL (store)));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (*results_list), FALSE);
#ifdef GIMP_UNSTABLE
  gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (*results_list),
                                    COLUMN_TOOLTIP);
#endif

  cell = gtk_cell_renderer_pixbuf_new ();
  column = gtk_tree_view_column_new_with_attributes (NULL, cell,
                                                     "icon-name", COLUMN_ICON,
                                                     "sensitive", COLUMN_SENSITIVE,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (*results_list), column);
  gtk_tree_view_column_set_min_width (column, 22);

  cell = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (NULL, cell,
                                                     "markup",    COLUMN_MARKUP,
                                                     "sensitive", COLUMN_SENSITIVE,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (*results_list), column);
  gtk_tree_view_column_set_max_width (column, wid1);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (*list_view),
                                  GTK_POLICY_NEVER,
                                  GTK_POLICY_AUTOMATIC);

  gtk_container_add (GTK_CONTAINER (*list_view), *results_list);
  g_object_unref (G_OBJECT (store));
}

static void
search_dialog_free (SearchDialog *private)
{
  g_slice_free (SearchDialog, private);
}
