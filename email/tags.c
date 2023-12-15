/**
 * @file
 * Driver based email tags
 *
 * @authors
 * Copyright (C) 2017 Mehdi Abaakouk <sileht@sileht.net>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page email_tags Email tags
 *
 * Driver based email tags
 */

#include "config.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "mutt/lib.h"
#include "config/lib.h"
#include "core/lib.h"
#include "tags.h"

struct HashTable *TagTransforms = NULL; ///< Hash Table: "inbox" -> "i" - Alternative tag names
struct HashTable *TagFormats = NULL; ///< Hash Table: "inbox" -> "GI" - Tag format strings

/**
 * tag_free - Free a Tag
 * @param ptr Tag to free
 */
void tag_free(struct Tag **ptr)
{
  if (!ptr || !*ptr)
    return;

  struct Tag *tag = *ptr;
  FREE(&tag->name);
  FREE(&tag->transformed);

  FREE(ptr);
}

/**
 * tag_new - Create a new Tag
 * @retval ptr New Tag
 */
struct Tag *tag_new(void)
{
  return mutt_mem_calloc(1, sizeof(struct Tag));
}

/**
 * driver_tags_getter - Get transformed tags
 * @param tl               List of tags
 * @param show_hidden      Show hidden tags
 * @param show_transformed Show transformed tags
 * @param filter           Match tags to this string
 * @retval ptr String list of tags
 *
 * Return a new allocated string containing tags separated by space
 */
static char *driver_tags_getter(struct TagList *tl, bool show_hidden,
                                bool show_transformed, const char *filter)
{
  if (!tl)
    return NULL;

  char *tags = NULL;
  struct Tag *tag = NULL;
  STAILQ_FOREACH(tag, tl, entries)
  {
    if (filter && !mutt_str_equal(tag->name, filter))
      continue;
    if (show_hidden || !tag->hidden)
    {
      if (show_transformed && tag->transformed)
        mutt_str_append_item(&tags, tag->transformed, ' ');
      else
        mutt_str_append_item(&tags, tag->name, ' ');
    }
  }
  return tags;
}

/**
 * driver_tags_add - Add a tag to header
 * @param[in] tl      List of tags
 * @param[in] new_tag String representing the new tag
 *
 * Add a tag to the header tags
 *
 * @note The ownership of the string is passed to the TagList structure
 */
void driver_tags_add(struct TagList *tl, char *new_tag)
{
  char *new_tag_transformed = mutt_hash_find(TagTransforms, new_tag);

  struct Tag *tag = tag_new();
  tag->name = new_tag;
  tag->hidden = false;
  tag->transformed = mutt_str_dup(new_tag_transformed);

  /* filter out hidden tags */
  const struct Slist *c_hidden_tags = cs_subset_slist(NeoMutt->sub, "hidden_tags");
  if (c_hidden_tags)
    if (mutt_list_find(&c_hidden_tags->head, new_tag))
      tag->hidden = true;

  STAILQ_INSERT_TAIL(tl, tag, entries);
}

/**
 * driver_tags_free - Free tags from a header
 * @param[in] tl List of tags
 *
 * Free the whole tags structure
 */
void driver_tags_free(struct TagList *tl)
{
  if (!tl)
    return;

  struct Tag *tag = STAILQ_FIRST(tl);
  struct Tag *next = NULL;
  while (tag)
  {
    next = STAILQ_NEXT(tag, entries);
    tag_free(&tag);
    tag = next;
  }
  STAILQ_INIT(tl);
}

/**
 * driver_tags_get_transformed - Get transformed tags
 * @param[in] tl List of tags
 * @retval ptr String list of tags
 *
 * Return a new allocated string containing all tags separated by space with
 * transformation
 */
char *driver_tags_get_transformed(struct TagList *tl)
{
  return driver_tags_getter(tl, false, true, NULL);
}

/**
 * driver_tags_get - Get tags
 * @param[in] tl List of tags
 * @retval ptr String list of tags
 *
 * Return a new allocated string containing all tags separated by space
 */
char *driver_tags_get(struct TagList *tl)
{
  return driver_tags_getter(tl, false, false, NULL);
}

/**
 * driver_tags_get_with_hidden - Get tags with hiddens
 * @param[in] tl List of tags
 * @retval ptr String list of tags
 *
 * Return a new allocated string containing all tags separated by space even
 * the hiddens.
 */
char *driver_tags_get_with_hidden(struct TagList *tl)
{
  return driver_tags_getter(tl, true, false, NULL);
}

/**
 * driver_tags_get_transformed_for - Get transformed tag for a tag name from a header
 * @param[in] tl   List of tags
 * @param[in] name Tag to transform
 * @retval ptr String tag
 *
 * Return a new allocated string containing all tags separated by space even
 * the hiddens.
 */
char *driver_tags_get_transformed_for(struct TagList *tl, const char *name)
{
  return driver_tags_getter(tl, true, true, name);
}

/**
 * driver_tags_replace - Replace all tags
 * @param[in] tl    List of tags
 * @param[in] tags string of all tags separated by space
 * @retval false No changes are made
 * @retval true  Tags are updated
 *
 * Free current tags structures and replace it by new tags
 */
bool driver_tags_replace(struct TagList *tl, const char *tags)
{
  if (!tl)
    return false;

  driver_tags_free(tl);

  if (tags)
  {
    struct ListHead hsplit = STAILQ_HEAD_INITIALIZER(hsplit);
    mutt_list_str_split(&hsplit, tags, ' ');
    struct ListNode *np = NULL;
    STAILQ_FOREACH(np, &hsplit, entries)
    {
      driver_tags_add(tl, np->data);
    }
    mutt_list_clear(&hsplit);
  }
  return true;
}

/**
 * tags_deleter - Free our hash table data - Implements ::hash_hdata_free_t - @ingroup hash_hdata_free_api
 */
static void tags_deleter(int type, void *obj, intptr_t data)
{
  FREE(&obj);
}

/**
 * driver_tags_init - Initialize structures used for tags
 */
void driver_tags_init(void)
{
  TagTransforms = mutt_hash_new(64, MUTT_HASH_STRCASECMP | MUTT_HASH_STRDUP_KEYS);
  mutt_hash_set_destructor(TagTransforms, tags_deleter, 0);

  TagFormats = mutt_hash_new(64, MUTT_HASH_STRDUP_KEYS);
  mutt_hash_set_destructor(TagFormats, tags_deleter, 0);
}

/**
 * driver_tags_cleanup - Deinitialize structures used for tags
 */
void driver_tags_cleanup(void)
{
  mutt_hash_free(&TagFormats);
  mutt_hash_free(&TagTransforms);
}
