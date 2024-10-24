/**
 * @file
 * Test code for Empty if-else Expandos
 *
 * @authors
 * Copyright (C) 2023-2024 Tóth János <gomba007@gmail.com>
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
#include <stddef.h>
#include "expando/lib.h"
#include "common.h" // IWYU pragma: keep

void test_expando_empty_if_else(void)
{
  static const struct ExpandoDefinition TestFormatDef[] = {
    // clang-format off
    { "c", "cherry",    1, 2, E_TYPE_STRING, NULL },
    { "f", "fig",       1, 2, E_TYPE_STRING, NULL },
    { "t", "tangerine", 1, 3, E_TYPE_STRING, NULL },
    { NULL, NULL, 0, -1, -1, NULL }
    // clang-format on
  };
  struct ExpandoParseError err = { 0 };

  const char *input1 = "%<c?>";
  struct ExpandoNode *root1 = NULL;
  node_tree_parse(&root1, input1, TestFormatDef, &err);
  TEST_CHECK(err.position == NULL);
  {
    struct ExpandoNode *node = get_nth_node(root1, 0);
    check_node_cond(node);

    struct ExpandoNode *node_cond = node_get_child(node, ENC_CONDITION);
    struct ExpandoNode *node_true = node_get_child(node, ENC_TRUE);
    struct ExpandoNode *node_false = node_get_child(node, ENC_FALSE);

    check_node_condbool(node_cond, "c");
    check_node_empty(node_true);
    TEST_CHECK(node_false == NULL);
  }
  node_tree_free(&root1);

  const char *input2 = "%<c?&>";
  struct ExpandoNode *root2 = NULL;
  node_tree_parse(&root2, input2, TestFormatDef, &err);
  TEST_CHECK(err.position == NULL);
  {
    struct ExpandoNode *node = get_nth_node(root2, 0);
    check_node_cond(node);

    struct ExpandoNode *node_cond = node_get_child(node, ENC_CONDITION);
    struct ExpandoNode *node_true = node_get_child(node, ENC_TRUE);
    struct ExpandoNode *node_false = node_get_child(node, ENC_FALSE);

    check_node_condbool(node_cond, "c");
    check_node_empty(node_true);
    check_node_empty(node_false);
  }
  node_tree_free(&root2);

  const char *input3 = "%<c?%t&>";
  struct ExpandoNode *root3 = NULL;
  node_tree_parse(&root3, input3, TestFormatDef, &err);
  TEST_CHECK(err.position == NULL);
  {
    struct ExpandoNode *node = get_nth_node(root3, 0);
    check_node_cond(node);

    struct ExpandoNode *node_cond = node_get_child(node, ENC_CONDITION);
    struct ExpandoNode *node_true = node_get_child(node, ENC_TRUE);
    struct ExpandoNode *node_false = node_get_child(node, ENC_FALSE);

    check_node_condbool(node_cond, "c");
    check_node_expando(node_true, "t", NULL);
    check_node_empty(node_false);
  }
  node_tree_free(&root3);

  const char *input4 = "%<c?&%f>";
  struct ExpandoNode *root4 = NULL;
  node_tree_parse(&root4, input4, TestFormatDef, &err);
  TEST_CHECK(err.position == NULL);
  {
    struct ExpandoNode *node = get_nth_node(root4, 0);
    check_node_cond(node);

    struct ExpandoNode *node_cond = node_get_child(node, ENC_CONDITION);
    struct ExpandoNode *node_true = node_get_child(node, ENC_TRUE);
    struct ExpandoNode *node_false = node_get_child(node, ENC_FALSE);

    check_node_condbool(node_cond, "c");
    check_node_empty(node_true);
    check_node_expando(node_false, "f", NULL);
  }
  node_tree_free(&root4);
}
