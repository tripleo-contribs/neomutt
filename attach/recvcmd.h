/**
 * @file
 * Send/reply with an attachment
 *
 * @authors
 * Copyright (C) 2018-2023 Richard Russon <rich@flatcap.org>
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

#ifndef MUTT_ATTACH_RECVCMD_H
#define MUTT_ATTACH_RECVCMD_H

#include "send/lib.h"

struct AttachCtx;
struct AttachPtrArray;
struct Email;
struct Mailbox;

void attach_bounce_message  (struct AttachPtrArray *aa, struct Mailbox *m);
void mutt_attach_resend     (struct AttachPtrArray *aa, struct Mailbox *m);
void mutt_attach_forward    (struct AttachPtrArray *aa, struct Email *e, struct AttachCtx *actx, SendFlags flags);
void mutt_attach_reply      (struct AttachPtrArray *aa, struct Mailbox *m, struct Email *e, struct AttachCtx *actx, SendFlags flags);
void mutt_attach_mail_sender(struct AttachPtrArray *aa);

#endif /* MUTT_ATTACH_RECVCMD_H */
