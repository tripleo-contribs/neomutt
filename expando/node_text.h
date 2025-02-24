/**
 * @file
 * Expando Node for Text
 *
 * @authors
 * Copyright (C) 2023-2024 Tóth János <gomba007@gmail.com>
 * Copyright (C) 2023-2024 Richard Russon <rich@flatcap.org>
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

#ifndef MUTT_EXPANDO_NODE_TEXT_H
#define MUTT_EXPANDO_NODE_TEXT_H

#include <stdint.h>

/**
 * typedef NodeTextTermFlags - Special characters that end a text string
 */
typedef uint8_t NodeTextTermFlags;    ///< Flags, e.g. #NTE_NO_FLAGS
#define NTE_NO_FLAGS               0  ///< No flags are set
#define NTE_AMPERSAND       (1 <<  0) ///< '&' Ampersand
#define NTE_GREATER         (1 <<  1) ///< '>' Greater than
#define NTE_QUESTION        (1 <<  2) ///< '?' Question mark

struct ExpandoNode *node_text_new(const char *text);
struct ExpandoNode *node_text_parse(const char *str, NodeTextTermFlags term_chars, const char **parsed_until);

#endif /* MUTT_EXPANDO_NODE_TEXT_H */
