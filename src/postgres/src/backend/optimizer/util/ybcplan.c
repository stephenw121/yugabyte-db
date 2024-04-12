/*--------------------------------------------------------------------------------------------------
 *
 * ybcplan.c
 *	  Utilities for YugaByte scan.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/backend/executor/ybcplan.c
 *
 *--------------------------------------------------------------------------------------------------
 */


#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "executor/ybcExpr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "nodes/print.h"
#include "nodes/relation.h"
#include "utils/datum.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"

/* YB includes. */
#include "catalog/pg_am_d.h"
#include "catalog/yb_catalog_version.h"
#include "optimizer/ybcplan.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/*
 * Check if statement can be implemented by a single request to the DocDB.
 *
 * An insert, update, or delete command makes one or more write requests to
 * the DocDB to apply the changes, and may also make read requests to find
 * the target row, its id, current values, etc. Complex expressions (e.g.
 * subqueries, stored functions) may also make requests to DocDB.
 *
 * Typically multiple requests require a transaction to maintain consistency.
 * However, if the command is about to make single write request, it is OK to
 * skip the transaction. The ModifyTable plan node makes one write request per
 * row it fetches from its subplans, therefore the key criteria of single row
 * modify is a single Result plan node in the ModifyTable's plans list.
 * Plain Result plan node produces exactly one row without making requests to
 * the DocDB, unless it has a subplan or complex expressions to evaluate.
 *
 * Full list of the conditions we check here:
 *  - there is only one target table;
 *  - there is no ON CONFLICT clause;
 *  - there is no init plan;
 *  - there is only one source plan, which is a simple form of Result;
 *  - all expressions in the Result's target list and in the returning list are
 *    simple, that means they do not need to access the DocDB.
 *
 * Additionally, during execution we will also check:
 *  - not in transaction block;
 *  - is a single-plan execution;
 *  - target table has no triggers to fire;
 *  - target table has no indexes to update.
 * And if all are true we will execute this op as a single-row transaction
 * rather than a distributed transaction.
 */
static bool ModifyTableIsSingleRowWrite(ModifyTable *modifyTable)
{
	/* Support INSERT, UPDATE, and DELETE. */
	if (modifyTable->operation != CMD_INSERT &&
		modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/* Multi-relation implies multi-shard. */
	if (list_length(modifyTable->resultRelations) != 1)
		return false;

	/* ON CONFLICT clause may require another write request */
	if (modifyTable->onConflictAction != ONCONFLICT_NONE)
		return false;

	/* Init plan execution would require request(s) to DocDB */
	if (modifyTable->plan.initPlan != NIL)
		return false;

	/* Check the data source is a single plan */
	if (list_length(modifyTable->plans) != 1)
		return false;

	Plan *plan = (Plan *) linitial(modifyTable->plans);

	/*
	 * Only Result plan without a subplan produces single tuple without making
	 * DocDB requests
	 */
	if (!IsA(plan, Result) || outerPlan(plan))
		return false;

	/* Complex expressions in the target list may require DocDB requests */
	if (YbIsTransactionalExpr((Node *) plan->targetlist))
		return false;

	/* Same for the returning expressions */
	if (YbIsTransactionalExpr((Node *) modifyTable->returningLists))
		return false;

	/* If all our checks passed return true */
	return true;
}

bool YBCIsSingleRowModify(PlannedStmt *pstmt)
{
	if (pstmt->planTree && IsA(pstmt->planTree, ModifyTable))
	{
		ModifyTable *node = castNode(ModifyTable, pstmt->planTree);
		return ModifyTableIsSingleRowWrite(node);
	}

	return false;
}

/*
 * Returns true if this ModifyTable can be executed by a single RPC, without
 * an initial table scan fetching a target tuple.
 *
 * Right now, this is true iff:
 *  - it is UPDATE or DELETE command.
 *  - source data is a Result node (meaning we are skipping scan and thus
 *    are single row).
 */
bool YbCanSkipFetchingTargetTupleForModifyTable(ModifyTable *modifyTable)
{
	/* Support UPDATE and DELETE. */
	if (modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/* Should only have one data source. */
	if (list_length(modifyTable->plans) != 1)
		return false;

	/*
	 * Verify the single data source is a Result node and does not have outer plan.
	 * Note that Result node never has inner plan.
	 */
	if (!IsA(linitial(modifyTable->plans), Result) || outerPlan(linitial(modifyTable->plans)))
		return false;

	return true;
}

/*
 * Returns true if provided Bitmapset of attribute numbers
 * matches the primary key attribute numbers of the relation.
 * Expects YBGetFirstLowInvalidAttributeNumber to be subtracted from attribute numbers.
 */
bool YBCAllPrimaryKeysProvided(Relation rel, Bitmapset *attrs)
{
	if (bms_is_empty(attrs))
	{
		/*
		 * If we don't explicitly check for empty attributes it is possible
		 * for this function to improperly return true. This is because in the
		 * case where a table does not have any primary key attributes we will
		 * use a hidden RowId column which is not exposed to the PG side, so
		 * both the YB primary key attributes and the input attributes would
		 * appear empty and would be equal, even though this is incorrect as
		 * the YB table has the hidden RowId primary key column.
		 */
		return false;
	}

	Bitmapset *primary_key_attrs = YBGetTablePrimaryKeyBms(rel);

	/* Verify the sets are the same. */
	return bms_equal(attrs, primary_key_attrs);
}

/*
 * is_index_only_refs
 *		Check if all column references from the list are available from the
 *		index described by the indexinfo.
 */
bool
is_index_only_refs(List *colrefs, IndexOptInfo *indexinfo, bool bitmapindex)
{
	ListCell *lc;
	foreach (lc, colrefs)
	{
		bool found = false;
		YbExprColrefDesc *colref = castNode(YbExprColrefDesc, lfirst(lc));
		for (int i = 0; i < indexinfo->ncolumns; i++)
		{
			if (colref->attno == indexinfo->indexkeys[i])
			{
				/*
				 * If index key can not return, it does not have actual value
				 * to evaluate the expression.
				 */
				if (indexinfo->canreturn[i])
				{
					found = true;
					break;
				}
				/*
				 * Special case for LSM bitmap index scans: in Yugabyte, these
				 * indexes claim they cannot return if they are a primary index.
				 * Generally it is simpler for primay indexes to pushdown their
				 * conditions at the table level, rather than the index level.
				 * Primary indexes don't need to first request ybctids, and then
				 * request rows matching the ybctids.
				 *
				 * However, bitmap index scans benefit from pushing down
				 * conditions to the index (whether its primary or secondary),
				 * because they collect ybctids from both. If we can filter
				 * out more ybctids earlier, it reduces network costs and the
				 * size of the ybctid bitmap.
				 */
				else if (IsYugaByteEnabled() && bitmapindex &&
						 indexinfo->relam == LSM_AM_OID)
				{
					Relation index;
					index = RelationIdGetRelation(indexinfo->indexoid);
					bool is_primary = index->rd_index->indisprimary;
					RelationClose(index);

					if (is_primary)
					{
						found = true;
						break;
					}
				}

				return false;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

/*
 * extract_pushdown_clauses
 *	  Extract actual clauses from RestrictInfo list and distribute them
 * 	  between three groups:
 *	  - local_quals - conditions not eligible for pushdown. They are evaluated
 *	  on the Postgres side on the rows fetched from DocDB;
 *	  - rel_remote_quals - conditions to pushdown with the request to the main
 *	  scanned relation. In the case of sequential scan or index only scan
 *	  the DocDB table or DocDB index respectively is the main (and only)
 *	  scanned relation, so the function returns only two groups;
 *	  - idx_remote_quals - conditions to pushdown with the request to the
 *	  secondary (index) relation. Used with the index scan on a secondary
 *	  index, and caller must provide IndexOptInfo record for the index.
 *	  - rel_colrefs, idx_colrefs are columns referenced by respective
 *	  rel_remote_quals or idx_remote_quals.
 *	  The output parameters local_quals, rel_remote_quals, rel_colrefs must
 *	  point to valid lists. The output parameters idx_remote_quals and
 *	  idx_colrefs may be NULL if the indexinfo is NULL.
 */
void
extract_pushdown_clauses(List *restrictinfo_list,
						 IndexOptInfo *indexinfo,
						 bool bitmapindex,
						 List **local_quals,
						 List **rel_remote_quals,
						 List **rel_colrefs,
						 List **idx_remote_quals,
						 List **idx_colrefs)
{
	ListCell *lc;
	foreach(lc, restrictinfo_list)
	{
		RestrictInfo *ri = lfirst_node(RestrictInfo, lc);
		/* ignore pseudoconstants */
		if (ri->pseudoconstant)
			continue;

		if (ri->yb_pushable)
		{
			List *colrefs = NIL;
			bool pushable PG_USED_FOR_ASSERTS_ONLY;

			/*
			 * Find column references. It has already been determined that
			 * the expression is pushable.
			 */
			pushable = YbCanPushdownExpr(ri->clause, &colrefs);
			Assert(pushable);

			/*
			 * If there are both main and secondary (index) relations,
			 * determine one to pushdown the condition. It is more efficient
			 * to apply filter earlier, so prefer index, if it has all the
			 * necessary columns.
			 */
			if (indexinfo == NULL ||
				!is_index_only_refs(colrefs, indexinfo, bitmapindex))
			{
				*rel_colrefs = list_concat(*rel_colrefs, colrefs);
				*rel_remote_quals = lappend(*rel_remote_quals, ri->clause);
			}
			else
			{
				*idx_colrefs = list_concat(*idx_colrefs, colrefs);
				*idx_remote_quals = lappend(*idx_remote_quals, ri->clause);
			}
		}
		else
		{
			*local_quals = lappend(*local_quals, ri->clause);
		}
	}
}
