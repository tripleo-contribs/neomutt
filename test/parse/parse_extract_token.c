/**
 * @file
 * Test code for extracting tokens from strings
 *
 * @authors
 * Copyright (C) 2023 Richard Russon <rich@flatcap.org>
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
#include <string.h>
#include "parse/lib.h"
#include "test_common.h"

void test_parse_extract_token(void)
{
  // int parse_extract_token(struct Buffer *dest, struct Buffer *tok, TokenFlags flags);

  TEST_CASE("parse_extract_token");
  int rc = parse_extract_token(NULL, NULL, TOKEN_NO_FLAGS);
  TEST_CHECK_NUM_EQ(rc, -1);
}
