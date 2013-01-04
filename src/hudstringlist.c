/*
 * Copyright © 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Ryan Lortie <desrt@desrt.ca>
 */

#include "hudstringlist.h"

#include <string.h>

/**
 * SECTION:hudstringlist
 * @title: HudStringList
 * @short_description: a refcounted list of strings
 *
 * #HudStringList is a refcounted list of strings.
 *
 * Borrowing heavily on conventions of many functional programming
 * languages, a list is a head element connected to a tail list (ie: the
 * rest of the items).
 *
 * A %NULL pointer is considered to be a valid empty list.
 *
 * Each list node is refcounted, and holds a reference on its 'tail'
 * list.  This allows common tails to be shared.
 *
 * This mechanism is ideally suited to the HUD which is interested in
 * displaying items of the form "File > New" and "File > Open".  In this
 * case, these items would be represented (in reverse) by the lists
 * <code>['Open', 'File']</code> and <code>['New', 'File']</code> with
 * the common tail portion shared between both items.
 *
 * Each #HudStringList node uses only one variable-sized block of
 * memory.  The reference count and pointer to the 'tail' are stored in
 * a header, followed by the 'head' string data.
 **/

/**
 * HudStringList:
 *
 * This is an opaque structure type.
 **/

struct _HudStringList
{
  HudStringList *tail;
  gint ref_count;

  /* variable length.  must come last! */
  gchar head[1];
};

/**
 * hud_string_list_unref:
 * @list: (allow-none): a #HudStringList, possibly %NULL
 *
 * Decreases the reference count on @list, possibly freeing it.
 **/
void
hud_string_list_unref (HudStringList *list)
{
  if (list && g_atomic_int_dec_and_test (&list->ref_count))
    {
      hud_string_list_unref (list->tail);
      g_free (list);
    }
}

/**
 * hud_string_list_ref:
 * @list: (allow-none): a #HudStringList, possibly %NULL
 *
 * Increases the reference count on @list.
 *
 * Returns: a new reference to the list
 **/
HudStringList *
hud_string_list_ref (HudStringList *list)
{
  if (list)
    g_atomic_int_inc (&list->ref_count);

  return list;
}

/**
 * hud_string_list_cons:
 * @head: a string for the head item
 * @tail: (allow-none): the tail #HudStringList, possibly %NULL
 *
 * Create a new list with @head as the first item and @tail as the rest
 * of the items.
 *
 * A reference is taken on @tail.
 *
 * Returns: (transfer full): a new list
 **/
HudStringList *
hud_string_list_cons (const gchar   *head,
                      HudStringList *tail)
{
  HudStringList *list;
  gsize headlen;

  headlen = strlen (head);

  list = g_malloc (G_STRUCT_OFFSET (HudStringList, head) + headlen + 1);
  list->tail = hud_string_list_ref (tail);
  /* coverity[secure_coding] */
  strcpy (list->head, head);
  list->ref_count = 1;

  return list;
}

/**
 * hud_string_list_get_head:
 * @list: a non-empty (non-%NULL) #HudStringList
 *
 * Gets the head string of the list.
 *
 * Returns: the head element, as a normal C string
 **/
const gchar *
hud_string_list_get_head (HudStringList *list)
{
  return list->head;
}

/**
 * hud_string_list_get_tail:
 * @list: a non-empty (non-%NULL) #HudStringList
 *
 * Gets the tail of the list.
 *
 * Returns: (transfer none): the tail of the list
 **/
HudStringList *
hud_string_list_get_tail (HudStringList *list)
{
  return list->tail;
}

/**
 * hud_string_list_pretty_print:
 * @list: (allow-none): a #HudStringList, possibly %NULL
 *
 * Pretty-prints the list.
 *
 * This function is intended only for debugging purposes.
 *
 * Returns: the pretty-printed list
 **/
gchar *
hud_string_list_pretty_print (HudStringList *list)
{
  GString *string;

  string = g_string_new (NULL);
  while (list)
    {
      g_string_prepend (string, list->head);

      list = list->tail;

      if (list)
        g_string_prepend (string, " > ");
    }

  return g_string_free (string, FALSE);
}

/**
 * hud_string_list_cons_label:
 * @label: (allow-none): a menuitem label
 * @tail: (allow-none): the tail #HudStringList, possibly %NULL
 *
 * Slight "magic" helper function for doing the right thing with
 * prepending menu labels.
 *
 * @label is processed, removing mnemonic prefixes (ie: '_' characters)
 * and then the function acts, essentially as hud_string_list_cons().
 *
 * Returns: (transfer full): a new #HudStringList
 **/
HudStringList *
hud_string_list_cons_label (const gchar   *label,
                            HudStringList *tail)
{
  HudStringList *list;
  gint i = 0, j = 0;
  gsize headlen;

  /* For simplicity, over-allocate.  In practice, this will only
   * ever waste one byte at most (since we will only remove one
   * underscore).
   */
  headlen = strlen (label);

  list = g_malloc (G_STRUCT_OFFSET (HudStringList, head) + headlen + 1);
  list->tail = hud_string_list_ref (tail);
  list->ref_count = 1;

  while (label[j])
    {
      if (label[j] == '_' && label[j + 1])
        j++;

      list->head[i++] = label[j++];
    }
  g_assert (i <= headlen);
  list->head[i] = '\0';

  return list;
}

HudStringList*
hud_string_list_add_item (const gchar *item, HudStringList *stringlist)
{
  HudStringList *new_list = hud_string_list_cons (item, stringlist);
  hud_string_list_unref (stringlist);
  return new_list;
}
