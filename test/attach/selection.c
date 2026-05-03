/**
 * @file
 * Test code for attachment selection helpers
 *
 * @authors
 * Copyright (C) 2026 Richard Russon <rich@flatcap.org>
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

#define TEST_NO_MAIN
#include "config.h"
#include "acutest.h"
#include <stdbool.h>
#include <stddef.h>
#include "mutt/lib.h"
#include "email/lib.h"
#include "attach/lib.h"
#include "menu/lib.h"
#include "test_common.h"

static void check_attach_selection(struct AttachPtrArray *aa,
                                   struct AttachPtr *expected[], size_t expected_len)
{
  TEST_CHECK_NUM_EQ(ARRAY_SIZE(aa), expected_len);

  for (size_t i = 0; i < expected_len; i++)
  {
    TEST_CHECK(*ARRAY_GET(aa, i) == expected[i]);
  }
}

static void check_body_selection(struct BodyArray *ba, struct Body *expected[], size_t expected_len)
{
  TEST_CHECK_NUM_EQ(ARRAY_SIZE(ba), expected_len);

  for (size_t i = 0; i < expected_len; i++)
  {
    TEST_CHECK(*ARRAY_GET(ba, i) == expected[i]);
  }
}

void test_aa_add_selection(void)
{
  // int aa_add_selection(struct AttachPtrArray *aa, struct AttachCtx *actx,
  //                      struct Menu *menu, bool use_tagged, int count);

  struct Body b1 = { 0 };
  struct Body b2 = { 0 };
  struct Body b3 = { 0 };
  struct Body b4 = { 0 };
  struct AttachPtr a1 = { .body = &b1 };
  struct AttachPtr a2 = { .body = &b2 };
  struct AttachPtr a3 = { .body = &b3 };
  struct AttachPtr a4 = { .body = &b4 };
  struct AttachPtr *idx[] = { &a1, &a2, &a3, &a4 };
  short v2r[] = { 2, 0, 3, 1 };
  struct AttachCtx actx = {
    .idx = idx,
    .idxlen = 4,
    .v2r = v2r,
    .vcount = 4,
  };
  struct Menu menu = { .current = 1 };

  {
    struct AttachPtrArray aa = ARRAY_HEAD_INITIALIZER;
    struct AttachPtr *expected[] = { &a1 };
    TEST_CHECK_NUM_EQ(aa_add_selection(&aa, &actx, &menu, false, 0), 1);
    check_attach_selection(&aa, expected, sizeof(expected) / sizeof(expected[0]));
    ARRAY_FREE(&aa);
  }

  {
    struct AttachPtrArray aa = ARRAY_HEAD_INITIALIZER;
    struct AttachPtr *expected[] = { &a1 };
    TEST_CHECK_NUM_EQ(aa_add_selection(&aa, &actx, &menu, false, 1), 1);
    check_attach_selection(&aa, expected, sizeof(expected) / sizeof(expected[0]));
    ARRAY_FREE(&aa);
  }

  {
    struct AttachPtrArray aa = ARRAY_HEAD_INITIALIZER;
    struct AttachPtr *expected[] = { &a1, &a4, &a2 };
    TEST_CHECK_NUM_EQ(aa_add_selection(&aa, &actx, &menu, false, 3), 3);
    check_attach_selection(&aa, expected, sizeof(expected) / sizeof(expected[0]));
    ARRAY_FREE(&aa);
  }

  {
    struct AttachPtrArray aa = ARRAY_HEAD_INITIALIZER;
    struct AttachPtr *expected[] = { &a1, &a4, &a2 };
    TEST_CHECK_NUM_EQ(aa_add_selection(&aa, &actx, &menu, false, 99), 3);
    check_attach_selection(&aa, expected, sizeof(expected) / sizeof(expected[0]));
    ARRAY_FREE(&aa);
  }

  {
    struct AttachPtrArray aa = ARRAY_HEAD_INITIALIZER;
    b2.tagged = true;
    b4.tagged = true;
    menu.current = 0;

    struct AttachPtr *expected[] = { &a2, &a4 };
    TEST_CHECK_NUM_EQ(aa_add_selection(&aa, &actx, &menu, true, 99), 2);
    check_attach_selection(&aa, expected, sizeof(expected) / sizeof(expected[0]));

    b2.tagged = false;
    b4.tagged = false;
    ARRAY_FREE(&aa);
  }
}

void test_ba_add_selection(void)
{
  // int ba_add_selection(struct BodyArray *ba, struct AttachCtx *actx,
  //                      struct Menu *menu, bool use_tagged, int count);

  struct Body b1 = { 0 };
  struct Body b2 = { 0 };
  struct Body b3 = { 0 };
  struct AttachPtr a1 = { .body = &b1 };
  struct AttachPtr a2 = { .body = &b2 };
  struct AttachPtr a3 = { .body = &b3 };
  struct AttachPtr *idx[] = { &a1, &a2, &a3 };
  short v2r[] = { 1, 2, 0 };
  struct AttachCtx actx = {
    .idx = idx,
    .idxlen = 3,
    .v2r = v2r,
    .vcount = 3,
  };
  struct Menu menu = { .current = 1 };

  struct BodyArray ba = ARRAY_HEAD_INITIALIZER;
  struct Body *expected[] = { &b3, &b1 };
  TEST_CHECK_NUM_EQ(ba_add_selection(&ba, &actx, &menu, false, 2), 2);
  check_body_selection(&ba, expected, sizeof(expected) / sizeof(expected[0]));
  ARRAY_FREE(&ba);
}
