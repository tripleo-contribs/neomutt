/**
 * @file
 * Maildir Mailbox
 *
 * @authors
 * Copyright (C) 2024 Richard Russon <rich@flatcap.org>
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
 * @page maildir_mailbox Maildir Mailbox
 *
 * Maildir Mailbox
 */

#include "config.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mutt/lib.h"
#include "config/lib.h"
#include "email/lib.h"
#include "mailbox.h"
#include "progress/lib.h"
#include "edata.h"
#include "hcache.h"
#include "mdata.h"
#include "mdemail.h"
#include "mx.h"
#include "shared.h"
#ifdef USE_INOTIFY
#include "monitor.h"
#endif

struct Progress;

// Flags for maildir_check()
#define MMC_NO_DIRS 0        ///< No directories changed
#define MMC_NEW_DIR (1 << 0) ///< 'new' directory changed
#define MMC_CUR_DIR (1 << 1) ///< 'cur' directory changed

/**
 * maildir_email_new - Create a Maildir Email
 * @retval ptr Newly created Email
 *
 * Create a new Email and attach MaildirEmailData.
 *
 * @note This should be freed using email_free()
 */
struct Email *maildir_email_new(void)
{
  struct Email *e = email_new();
  e->edata = maildir_edata_new();
  e->edata_free = maildir_edata_free;

  return e;
}

/**
 * maildir_parse_flags - Parse Maildir file flags
 * @param e    Email
 * @param path Path to email file
 */
void maildir_parse_flags(struct Email *e, const char *path)
{
  char *q = NULL;

  e->flagged = false;
  e->read = false;
  e->replied = false;

  struct MaildirEmailData *edata = maildir_edata_get(e);
  if (!edata)
  {
    e->edata = maildir_edata_new();
    edata = e->edata;
  }

  const char c_maildir_field_delimiter = *cc_maildir_field_delimiter();
  char *p = strrchr(path, c_maildir_field_delimiter);
  if (p && mutt_str_startswith(p + 1, "2,"))
  {
    p += 3;

    mutt_str_replace(&edata->custom_flags, p);
    q = edata->custom_flags;

    while (*p)
    {
      switch (*p)
      {
        case 'F': // Flagged
          e->flagged = true;
          break;

        case 'R': // Replied
          e->replied = true;
          break;

        case 'S': // Seen
          e->read = true;
          break;

        case 'T': // Trashed
        {
          const bool c_flag_safe = cs_subset_bool(NeoMutt->sub, "flag_safe");
          if (!e->flagged || !c_flag_safe)
          {
            e->trash = true;
            e->deleted = true;
          }
          break;
        }

        default:
          *q++ = *p;
          break;
      }
      p++;
    }
  }

  if (q == edata->custom_flags)
    FREE(&edata->custom_flags);
  else if (q)
    *q = '\0';
}

/**
 * maildir_parse_stream - Parse a Maildir message
 * @param fp     Message file handle
 * @param fname  Message filename
 * @param is_old true, if the email is old (read)
 * @param e      Email
 * @retval true Success
 *
 * Actually parse a maildir message.  This may also be used to fill
 * out a fake header structure generated by lazy maildir parsing.
 */
bool maildir_parse_stream(FILE *fp, const char *fname, bool is_old, struct Email *e)
{
  if (!fp || !fname || !e)
    return false;

  const long size = mutt_file_get_size_fp(fp);
  if (size == 0)
    return false;

  e->env = mutt_rfc822_read_header(fp, e, false, false);

  if (e->received == 0)
    e->received = e->date_sent;

  /* always update the length since we have fresh information available. */
  e->body->length = size - e->body->offset;

  e->index = -1;

  /* maildir stores its flags in the filename, so ignore the
   * flags in the header of the message */
  e->old = is_old;
  maildir_parse_flags(e, fname);

  return e;
}

/**
 * maildir_parse_message - Actually parse a maildir message
 * @param fname  Message filename
 * @param is_old true, if the email is old (read)
 * @param e      Email to populate
 * @retval true Success
 *
 * This may also be used to fill out a fake header structure generated by lazy
 * maildir parsing.
 */
bool maildir_parse_message(const char *fname, bool is_old, struct Email *e)
{
  if (!fname || !e)
    return false;

  FILE *fp = mutt_file_fopen(fname, "r");
  if (!fp)
    return false;

  bool rc = maildir_parse_stream(fp, fname, is_old, e);
  mutt_file_fclose(&fp);
  return rc;
}

/**
 * maildir_move_to_mailbox - Copy the Maildir list to the Mailbox
 * @param[in]  m   Mailbox
 * @param[out] mda Maildir array to copy, then free
 * @retval num Number of new emails
 * @retval 0   Error
 */
static int maildir_move_to_mailbox(struct Mailbox *m, const struct MdEmailArray *mda)
{
  if (!m)
    return 0;

  int oldmsgcount = m->msg_count;

  struct MdEmail *md = NULL;
  struct MdEmail **mdp = NULL;
  ARRAY_FOREACH(mdp, mda)
  {
    md = *mdp;
    mutt_debug(LL_DEBUG2, "Considering %s\n", NONULL(md->canon_fname));
    if (!md->email)
      continue;

    mutt_debug(LL_DEBUG2, "Adding header structure. Flags: %s%s%s%s%s\n",
               md->email->flagged ? "f" : "", md->email->deleted ? "D" : "",
               md->email->replied ? "r" : "", md->email->old ? "O" : "",
               md->email->read ? "R" : "");
    mx_alloc_memory(m, m->msg_count);

    m->emails[m->msg_count] = md->email;
    m->emails[m->msg_count]->index = m->msg_count;
    mailbox_size_add(m, md->email);

    md->email = NULL;
    m->msg_count++;
  }

  int num = 0;
  if (m->msg_count > oldmsgcount)
    num = m->msg_count - oldmsgcount;

  return num;
}

/**
 * maildir_sort_inode - Compare two Maildirs by inode number - Implements ::sort_t - @ingroup sort_api
 */
static int maildir_sort_inode(const void *a, const void *b, void *sdata)
{
  const struct MdEmail *ma = *(struct MdEmail **) a;
  const struct MdEmail *mb = *(struct MdEmail **) b;

  return mutt_numeric_cmp(ma->inode, mb->inode);
}

/**
 * maildir_parse_dir - Read a Maildir mailbox
 * @param[in]  m        Mailbox
 * @param[out] mda      Array for results
 * @param[in]  subdir   Subdirectory, e.g. 'new'
 * @param[in]  progress Progress bar
 * @retval  0 Success
 * @retval -1 Error
 * @retval -2 Aborted
 */
static int maildir_parse_dir(struct Mailbox *m, struct MdEmailArray *mda,
                             const char *subdir, struct Progress *progress)
{
  struct dirent *de = NULL;
  int rc = 0;
  bool is_old = false;
  struct MdEmail *entry = NULL;
  struct Email *e = NULL;

  struct Buffer *buf = buf_pool_get();

  buf_printf(buf, "%s/%s", mailbox_path(m), subdir);
  is_old = mutt_str_equal("cur", subdir);

  DIR *dir = mutt_file_opendir(buf_string(buf), MUTT_OPENDIR_CREATE);
  if (!dir)
  {
    rc = -1;
    goto cleanup;
  }

  while (((de = readdir(dir))) && !SigInt)
  {
    if (*de->d_name == '.')
      continue;

    mutt_debug(LL_DEBUG2, "queueing %s\n", de->d_name);

    e = maildir_email_new();
    e->old = is_old;
    maildir_parse_flags(e, de->d_name);

    progress_update(progress, ARRAY_SIZE(mda) + 1, -1);

    buf_printf(buf, "%s/%s", subdir, de->d_name);
    e->path = buf_strdup(buf);

    entry = maildir_entry_new();
    entry->email = e;
    entry->inode = de->d_ino;
    ARRAY_ADD(mda, entry);
  }

  closedir(dir);

  if (SigInt)
  {
    SigInt = false;
    return -2; /* action aborted */
  }

  ARRAY_SORT(mda, maildir_sort_inode, NULL);

cleanup:
  buf_pool_release(&buf);

  return rc;
}

/**
 * maildir_delayed_parsing - This function does the second parsing pass
 * @param[in]  m   Mailbox
 * @param[out] mda Maildir array to parse
 * @param[in]  progress Progress bar
 */
static void maildir_delayed_parsing(struct Mailbox *m, struct MdEmailArray *mda,
                                    struct Progress *progress)
{
  char fn[PATH_MAX] = { 0 };

  struct HeaderCache *hc = maildir_hcache_open(m);

  struct MdEmail *md = NULL;
  struct MdEmail **mdp = NULL;
  ARRAY_FOREACH(mdp, mda)
  {
    md = *mdp;
    if (!md || !md->email || md->header_parsed)
      continue;

    progress_update(progress, ARRAY_FOREACH_IDX_mdp, -1);

    snprintf(fn, sizeof(fn), "%s/%s", mailbox_path(m), md->email->path);

    struct Email *e = maildir_hcache_read(hc, md->email, fn);
    if (e)
    {
      email_free(&md->email);
      md->email = e;
    }
    else
    {
      if (maildir_parse_message(fn, md->email->old, md->email))
      {
        md->header_parsed = true;
        maildir_hcache_store(hc, md->email);
      }
      else
      {
        email_free(&md->email);
      }
    }
  }

  maildir_hcache_close(&hc);
}

/**
 * maildir_check_dir - Check for new mail / mail counts
 * @param m           Mailbox to check
 * @param dir_name    Path to Mailbox
 * @param check_new   if true, check for new mail
 * @param check_stats if true, count total, new, and flagged messages
 *
 * Checks the specified maildir subdir (cur or new) for new mail or mail counts.
 */
static void maildir_check_dir(struct Mailbox *m, const char *dir_name,
                              bool check_new, bool check_stats)
{
  DIR *dir = NULL;
  struct dirent *de = NULL;
  char *p = NULL;
  struct stat st = { 0 };

  struct Buffer *path = buf_pool_get();
  struct Buffer *msgpath = buf_pool_get();
  buf_printf(path, "%s/%s", mailbox_path(m), dir_name);

  /* when $mail_check_recent is set, if the new/ directory hasn't been modified since
   * the user last exited the mailbox, then we know there is no recent mail.  */
  const bool c_mail_check_recent = cs_subset_bool(NeoMutt->sub, "mail_check_recent");
  if (check_new && c_mail_check_recent)
  {
    if ((stat(buf_string(path), &st) == 0) &&
        (mutt_file_stat_timespec_compare(&st, MUTT_STAT_MTIME, &m->last_visited) < 0))
    {
      check_new = false;
    }
  }

  if (!(check_new || check_stats))
    goto cleanup;

  dir = mutt_file_opendir(buf_string(path), MUTT_OPENDIR_CREATE);
  if (!dir)
  {
    m->type = MUTT_UNKNOWN;
    goto cleanup;
  }

  const char c_maildir_field_delimiter = *cc_maildir_field_delimiter();

  char delimiter_version[8] = { 0 };
  snprintf(delimiter_version, sizeof(delimiter_version), "%c2,", c_maildir_field_delimiter);
  while ((de = readdir(dir)))
  {
    if (*de->d_name == '.')
      continue;

    p = strstr(de->d_name, delimiter_version);
    if (p && strchr(p + 3, 'T'))
      continue;

    if (check_stats)
    {
      m->msg_count++;
      if (p && strchr(p + 3, 'F'))
        m->msg_flagged++;
    }
    if (!p || !strchr(p + 3, 'S'))
    {
      if (check_stats)
        m->msg_unread++;
      if (check_new)
      {
        if (c_mail_check_recent)
        {
          buf_printf(msgpath, "%s/%s", buf_string(path), de->d_name);
          /* ensure this message was received since leaving this m */
          if ((stat(buf_string(msgpath), &st) == 0) &&
              (mutt_file_stat_timespec_compare(&st, MUTT_STAT_CTIME, &m->last_visited) <= 0))
          {
            continue;
          }
        }
        m->has_new = true;
        if (check_stats)
        {
          m->msg_new++;
        }
        else
        {
          break;
        }
      }
    }
  }

  closedir(dir);

cleanup:
  buf_pool_release(&path);
  buf_pool_release(&msgpath);
}

/**
 * maildir_read_dir - Read a Maildir style mailbox
 * @param m      Mailbox
 * @param subdir Subdir of the maildir mailbox to read from
 * @retval  0 Success
 * @retval -1 Failure
 */
static int maildir_read_dir(struct Mailbox *m, const char *subdir)
{
  if (!m)
    return -1;

  mutt_path_tidy(&m->pathbuf, true);

  struct Progress *progress = NULL;

  if (m->verbose)
  {
    progress = progress_new(MUTT_PROGRESS_READ, 0);
    progress_set_message(progress, _("Scanning %s..."), mailbox_path(m));
  }

  struct MaildirMboxData *mdata = maildir_mdata_get(m);
  if (!mdata)
  {
    mdata = maildir_mdata_new();
    m->mdata = mdata;
    m->mdata_free = maildir_mdata_free;
  }

  struct MdEmailArray mda = ARRAY_HEAD_INITIALIZER;
  int rc = maildir_parse_dir(m, &mda, subdir, progress);
  progress_free(&progress);
  if (rc < 0)
    return -1;

  if (m->verbose)
  {
    progress = progress_new(MUTT_PROGRESS_READ, ARRAY_SIZE(&mda));
    progress_set_message(progress, _("Reading %s..."), mailbox_path(m));
  }
  maildir_delayed_parsing(m, &mda, progress);
  progress_free(&progress);

  maildir_move_to_mailbox(m, &mda);
  maildirarray_clear(&mda);

  if (!mdata->umask)
    mdata->umask = maildir_umask(m);

  return 0;
}

/**
 * maildir_check - Check for new mail
 * @param m Mailbox
 * @retval enum #MxStatus
 *
 * This function handles arrival of new mail and reopening of maildir folders.
 * The basic idea here is we check to see if either the new or cur
 * subdirectories have changed, and if so, we scan them for the list of files.
 * We check for newly added messages, and then merge the flags messages we
 * already knew about.  We don't treat either subdirectory differently, as mail
 * could be copied directly into the cur directory from another agent.
 */
static enum MxStatus maildir_check(struct Mailbox *m)
{
  struct stat st_new = { 0 }; /* status of the "new" subdirectory */
  struct stat st_cur = { 0 }; /* status of the "cur" subdirectory */
  int changed = MMC_NO_DIRS;  /* which subdirectories have changed */
  bool occult = false;        /* messages were removed from the mailbox */
  int num_new = 0;            /* number of new messages added to the mailbox */
  bool flags_changed = false; /* message flags were changed in the mailbox */
  struct HashTable *hash_names = NULL; // Hash Table: "base-filename" -> MdEmail
  struct MaildirMboxData *mdata = maildir_mdata_get(m);

  const bool c_check_new = cs_subset_bool(NeoMutt->sub, "check_new");
  if (!c_check_new)
    return MX_STATUS_OK;

  struct Buffer *buf = buf_pool_get();
  buf_printf(buf, "%s/new", mailbox_path(m));
  if (stat(buf_string(buf), &st_new) == -1)
  {
    buf_pool_release(&buf);
    return MX_STATUS_ERROR;
  }

  buf_printf(buf, "%s/cur", mailbox_path(m));
  if (stat(buf_string(buf), &st_cur) == -1)
  {
    buf_pool_release(&buf);
    return MX_STATUS_ERROR;
  }

  /* determine which subdirectories need to be scanned */
  if (mutt_file_stat_timespec_compare(&st_new, MUTT_STAT_MTIME, &mdata->mtime) > 0)
    changed = MMC_NEW_DIR;
  if (mutt_file_stat_timespec_compare(&st_cur, MUTT_STAT_MTIME, &mdata->mtime_cur) > 0)
    changed |= MMC_CUR_DIR;

  if (changed == MMC_NO_DIRS)
  {
    buf_pool_release(&buf);
    return MX_STATUS_OK; /* nothing to do */
  }

  /* Update the modification times on the mailbox.
   *
   * The monitor code notices changes in the open mailbox too quickly.
   * In practice, this sometimes leads to all the new messages not being
   * noticed during the SAME group of mtime stat updates.  To work around
   * the problem, don't update the stat times for a monitor caused check. */
#ifdef USE_INOTIFY
  if (MonitorCurMboxChanged)
  {
    MonitorCurMboxChanged = false;
  }
  else
#endif
  {
    mutt_file_get_stat_timespec(&mdata->mtime_cur, &st_cur, MUTT_STAT_MTIME);
    mutt_file_get_stat_timespec(&mdata->mtime, &st_new, MUTT_STAT_MTIME);
  }

  /* do a fast scan of just the filenames in
   * the subdirectories that have changed.  */
  struct MdEmailArray mda = ARRAY_HEAD_INITIALIZER;
  if (changed & MMC_NEW_DIR)
    maildir_parse_dir(m, &mda, "new", NULL);
  if (changed & MMC_CUR_DIR)
    maildir_parse_dir(m, &mda, "cur", NULL);

  /* we create a hash table keyed off the canonical (sans flags) filename
   * of each message we scanned.  This is used in the loop over the
   * existing messages below to do some correlation.  */
  hash_names = mutt_hash_new(ARRAY_SIZE(&mda), MUTT_HASH_NO_FLAGS);

  struct MdEmail *md = NULL;
  struct MdEmail **mdp = NULL;
  ARRAY_FOREACH(mdp, &mda)
  {
    md = *mdp;
    maildir_canon_filename(buf, md->email->path);
    md->canon_fname = buf_strdup(buf);
    mutt_hash_insert(hash_names, md->canon_fname, md);
  }

  /* check for modifications and adjust flags */
  for (int i = 0; i < m->msg_count; i++)
  {
    struct Email *e = m->emails[i];
    if (!e)
      break;

    maildir_canon_filename(buf, e->path);
    md = mutt_hash_find(hash_names, buf_string(buf));
    if (md && md->email)
    {
      /* message already exists, merge flags */

      /* check to see if the message has moved to a different
       * subdirectory.  If so, update the associated filename.  */
      if (!mutt_str_equal(e->path, md->email->path))
        mutt_str_replace(&e->path, md->email->path);

      /* if the user hasn't modified the flags on this message, update
       * the flags we just detected.  */
      if (!e->changed)
        if (maildir_update_flags(m, e, md->email))
          flags_changed = true;

      if (e->deleted == e->trash)
      {
        if (e->deleted != md->email->deleted)
        {
          e->deleted = md->email->deleted;
          flags_changed = true;
        }
      }
      e->trash = md->email->trash;

      /* this is a duplicate of an existing email, so remove it */
      email_free(&md->email);
    }
    /* This message was not in the list of messages we just scanned.
     * Check to see if we have enough information to know if the
     * message has disappeared out from underneath us.  */
    else if (((changed & MMC_NEW_DIR) && mutt_strn_equal(e->path, "new/", 4)) ||
             ((changed & MMC_CUR_DIR) && mutt_strn_equal(e->path, "cur/", 4)))
    {
      /* This message disappeared, so we need to simulate a "reopen"
       * event.  We know it disappeared because we just scanned the
       * subdirectory it used to reside in.  */
      occult = true;
      e->deleted = true;
      e->purge = true;
    }
    else
    {
      /* This message resides in a subdirectory which was not
       * modified, so we assume that it is still present and
       * unchanged.  */
    }
  }

  /* destroy the file name hash */
  mutt_hash_free(&hash_names);

  /* If we didn't just get new mail, update the tables. */
  if (occult)
    mailbox_changed(m, NT_MAILBOX_RESORT);

  /* do any delayed parsing we need to do. */
  maildir_delayed_parsing(m, &mda, NULL);

  /* Incorporate new messages */
  num_new = maildir_move_to_mailbox(m, &mda);
  maildirarray_clear(&mda);

  if (num_new > 0)
  {
    mailbox_changed(m, NT_MAILBOX_INVALID);
    m->changed = true;
  }

  buf_pool_release(&buf);

  ARRAY_FREE(&mda);
  if (occult)
    return MX_STATUS_REOPENED;
  if (num_new > 0)
    return MX_STATUS_NEW_MAIL;
  if (flags_changed)
    return MX_STATUS_FLAGS;
  return MX_STATUS_OK;
}

/**
 * maildir_update_mtime - Update our record of the Maildir modification time
 * @param m Mailbox
 */
void maildir_update_mtime(struct Mailbox *m)
{
  char buf[PATH_MAX] = { 0 };
  struct stat st = { 0 };
  struct MaildirMboxData *mdata = maildir_mdata_get(m);

  snprintf(buf, sizeof(buf), "%s/%s", mailbox_path(m), "cur");
  if (stat(buf, &st) == 0)
    mutt_file_get_stat_timespec(&mdata->mtime_cur, &st, MUTT_STAT_MTIME);

  snprintf(buf, sizeof(buf), "%s/%s", mailbox_path(m), "new");
  if (stat(buf, &st) == 0)
    mutt_file_get_stat_timespec(&mdata->mtime, &st, MUTT_STAT_MTIME);
}

// Mailbox API -----------------------------------------------------------------

/**
 * maildir_mbox_open - Open a Mailbox - Implements MxOps::mbox_open() - @ingroup mx_mbox_open
 */
enum MxOpenReturns maildir_mbox_open(struct Mailbox *m)
{
  if ((maildir_read_dir(m, "new") == -1) || (maildir_read_dir(m, "cur") == -1))
    return MX_OPEN_ERROR;

  return MX_OPEN_OK;
}

/**
 * maildir_mbox_open_append - Open a Mailbox for appending - Implements MxOps::mbox_open_append() - @ingroup mx_mbox_open_append
 */
bool maildir_mbox_open_append(struct Mailbox *m, OpenMailboxFlags flags)
{
  if (!(flags & (MUTT_APPEND | MUTT_APPENDNEW)))
  {
    return true;
  }

  errno = 0;
  if ((mutt_file_mkdir(mailbox_path(m), S_IRWXU) != 0) && (errno != EEXIST))
  {
    mutt_perror("%s", mailbox_path(m));
    return false;
  }

  char tmp[PATH_MAX] = { 0 };
  snprintf(tmp, sizeof(tmp), "%s/cur", mailbox_path(m));
  errno = 0;
  if ((mkdir(tmp, S_IRWXU) != 0) && (errno != EEXIST))
  {
    mutt_perror("%s", tmp);
    rmdir(mailbox_path(m));
    return false;
  }

  snprintf(tmp, sizeof(tmp), "%s/new", mailbox_path(m));
  errno = 0;
  if ((mkdir(tmp, S_IRWXU) != 0) && (errno != EEXIST))
  {
    mutt_perror("%s", tmp);
    snprintf(tmp, sizeof(tmp), "%s/cur", mailbox_path(m));
    rmdir(tmp);
    rmdir(mailbox_path(m));
    return false;
  }

  snprintf(tmp, sizeof(tmp), "%s/tmp", mailbox_path(m));
  errno = 0;
  if ((mkdir(tmp, S_IRWXU) != 0) && (errno != EEXIST))
  {
    mutt_perror("%s", tmp);
    snprintf(tmp, sizeof(tmp), "%s/cur", mailbox_path(m));
    rmdir(tmp);
    snprintf(tmp, sizeof(tmp), "%s/new", mailbox_path(m));
    rmdir(tmp);
    rmdir(mailbox_path(m));
    return false;
  }

  return true;
}

/**
 * maildir_mbox_check - Check for new mail - Implements MxOps::mbox_check() - @ingroup mx_mbox_check
 */
enum MxStatus maildir_mbox_check(struct Mailbox *m)
{
  return maildir_check(m);
}

/**
 * maildir_mbox_check_stats - Check the Mailbox statistics - Implements MxOps::mbox_check_stats() - @ingroup mx_mbox_check_stats
 */
enum MxStatus maildir_mbox_check_stats(struct Mailbox *m, uint8_t flags)
{
  bool check_stats = flags & MUTT_MAILBOX_CHECK_STATS;
  bool check_new = true;

  if (check_stats)
  {
    m->msg_new = 0;
    m->msg_count = 0;
    m->msg_unread = 0;
    m->msg_flagged = 0;
  }

  maildir_check_dir(m, "new", check_new, check_stats);

  const bool c_maildir_check_cur = cs_subset_bool(NeoMutt->sub, "maildir_check_cur");
  check_new = !m->has_new && c_maildir_check_cur;
  if (check_new || check_stats)
    maildir_check_dir(m, "cur", check_new, check_stats);

  return m->msg_new ? MX_STATUS_NEW_MAIL : MX_STATUS_OK;
}

/**
 * maildir_mbox_sync - Save changes to the Mailbox - Implements MxOps::mbox_sync() - @ingroup mx_mbox_sync
 * @retval enum #MxStatus
 *
 * @note The flag retvals come from a call to a backend sync function
 */
enum MxStatus maildir_mbox_sync(struct Mailbox *m)
{
  enum MxStatus check = maildir_check(m);
  if (check == MX_STATUS_ERROR)
    return check;

  struct HeaderCache *hc = maildir_hcache_open(m);

  struct Progress *progress = NULL;
  if (m->verbose)
  {
    progress = progress_new(MUTT_PROGRESS_WRITE, m->msg_count);
    progress_set_message(progress, _("Writing %s..."), mailbox_path(m));
  }

  for (int i = 0; i < m->msg_count; i++)
  {
    progress_update(progress, i, -1);

    struct Email *e = m->emails[i];
    if (!maildir_sync_mailbox_message(m, e, hc))
    {
      progress_free(&progress);
      goto err;
    }
  }
  progress_free(&progress);
  maildir_hcache_close(&hc);

  /* XXX race condition? */

  maildir_update_mtime(m);

  /* adjust indices */

  if (m->msg_deleted)
  {
    const bool c_maildir_trash = cs_subset_bool(NeoMutt->sub, "maildir_trash");
    for (int i = 0, j = 0; i < m->msg_count; i++)
    {
      struct Email *e = m->emails[i];
      if (!e)
        break;

      if (!e->deleted || c_maildir_trash)
        e->index = j++;
    }
  }

  return check;

err:
  maildir_hcache_close(&hc);
  return MX_STATUS_ERROR;
}

/**
 * maildir_mbox_close - Close a Mailbox - Implements MxOps::mbox_close() - @ingroup mx_mbox_close
 * @retval #MX_STATUS_OK Always
 */
enum MxStatus maildir_mbox_close(struct Mailbox *m)
{
  return MX_STATUS_OK;
}
