/* Copyright (c) 2020, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_JOIN_OPTIMIZER_WALK_ACCESS_PATHS_H
#define SQL_JOIN_OPTIMIZER_WALK_ACCESS_PATHS_H

#include "sql/join_optimizer/access_path.h"

/**
  Traverse every access path below `path` (limited to the current query block
  if cross_query_blocks is false), calling func() for each one with pre-
  or post-order traversal. If func() returns true, traversal is stopped early.

  The `join` parameter, signifying what query block `path` is part of, is only
  used if you set cross_query_blocks = false. It is used to know whether a
  MATERIALIZE or STREAM access path is part of the same query block or not.
  If you give join == nullptr and cross_query_blocks == false, the walk will
  always stop at such access paths.

  Nothing currently uses post-order traversal, but it has been requested for
  future use.
 */
template <class Func>
void WalkAccessPaths(AccessPath *path, const JOIN *join,
                     bool cross_query_blocks, Func &&func,
                     bool post_order_traversal = false) {
  if (!post_order_traversal) {
    if (func(path)) {
      // Stop recursing in this branch.
      return;
    }
  }
  switch (path->type) {
    case AccessPath::TABLE_SCAN:
    case AccessPath::INDEX_SCAN:
    case AccessPath::REF:
    case AccessPath::REF_OR_NULL:
    case AccessPath::EQ_REF:
    case AccessPath::PUSHED_JOIN_REF:
    case AccessPath::FULL_TEXT_SEARCH:
    case AccessPath::CONST_TABLE:
    case AccessPath::MRR:
    case AccessPath::FOLLOW_TAIL:
    case AccessPath::INDEX_RANGE_SCAN:
    case AccessPath::DYNAMIC_INDEX_RANGE_SCAN:
    case AccessPath::TABLE_VALUE_CONSTRUCTOR:
    case AccessPath::FAKE_SINGLE_ROW:
    case AccessPath::ZERO_ROWS:
    case AccessPath::ZERO_ROWS_AGGREGATED:
    case AccessPath::MATERIALIZED_TABLE_FUNCTION:
    case AccessPath::UNQUALIFIED_COUNT:
      // No children.
      return;
    case AccessPath::NESTED_LOOP_JOIN:
      WalkAccessPaths(path->nested_loop_join().outer, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      WalkAccessPaths(path->nested_loop_join().inner, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::NESTED_LOOP_SEMIJOIN_WITH_DUPLICATE_REMOVAL:
      WalkAccessPaths(path->nested_loop_semijoin_with_duplicate_removal().outer,
                      join, cross_query_blocks, std::forward<Func &&>(func),
                      post_order_traversal);
      WalkAccessPaths(path->nested_loop_semijoin_with_duplicate_removal().inner,
                      join, cross_query_blocks, std::forward<Func &&>(func),
                      post_order_traversal);
      break;
    case AccessPath::BKA_JOIN:
      WalkAccessPaths(path->bka_join().outer, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      WalkAccessPaths(path->bka_join().inner, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::HASH_JOIN:
      WalkAccessPaths(path->hash_join().outer, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      WalkAccessPaths(path->hash_join().inner, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::FILTER:
      WalkAccessPaths(path->filter().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::SORT:
      WalkAccessPaths(path->sort().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::AGGREGATE:
      WalkAccessPaths(path->aggregate().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::TEMPTABLE_AGGREGATE:
      WalkAccessPaths(path->temptable_aggregate().subquery_path, join,
                      cross_query_blocks, std::forward<Func &&>(func),
                      post_order_traversal);
      WalkAccessPaths(path->temptable_aggregate().table_path, join,
                      cross_query_blocks, std::forward<Func &&>(func),
                      post_order_traversal);
      break;
    case AccessPath::LIMIT_OFFSET:
      WalkAccessPaths(path->limit_offset().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::STREAM:
      if (cross_query_blocks || path->stream().join == join) {
        WalkAccessPaths(path->stream().child, join, cross_query_blocks,
                        std::forward<Func &&>(func), post_order_traversal);
      }
      break;
    case AccessPath::MATERIALIZE:
      WalkAccessPaths(path->materialize().table_path, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      for (const MaterializePathParameters::QueryBlock &query_block :
           path->materialize().param->query_blocks) {
        if (cross_query_blocks || query_block.join == join) {
          WalkAccessPaths(query_block.subquery_path, join, cross_query_blocks,
                          std::forward<Func &&>(func), post_order_traversal);
        }
      }
      break;
    case AccessPath::MATERIALIZE_INFORMATION_SCHEMA_TABLE:
      WalkAccessPaths(path->materialize_information_schema_table().table_path,
                      join, cross_query_blocks, std::forward<Func &&>(func),
                      post_order_traversal);
      break;
    case AccessPath::APPEND:
      if (cross_query_blocks) {
        for (const AppendPathParameters &child : *path->append().children) {
          WalkAccessPaths(child.path, join, cross_query_blocks,
                          std::forward<Func &&>(func), post_order_traversal);
        }
      }
      break;
    case AccessPath::WINDOWING:
      WalkAccessPaths(path->windowing().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::WEEDOUT:
      WalkAccessPaths(path->weedout().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::REMOVE_DUPLICATES:
      WalkAccessPaths(path->remove_duplicates().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::ALTERNATIVE:
      WalkAccessPaths(path->alternative().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
    case AccessPath::CACHE_INVALIDATOR:
      WalkAccessPaths(path->cache_invalidator().child, join, cross_query_blocks,
                      std::forward<Func &&>(func), post_order_traversal);
      break;
  }
  if (post_order_traversal) {
    if (func(path)) {
      // Stop recursing in this branch.
      return;
    }
  }
}

#endif  // SQL_JOIN_OPTIMIZER_WALK_ACCESS_PATHS_H
