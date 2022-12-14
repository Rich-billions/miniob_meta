/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Meiyi & Longda on 2021/4/13.
//

#include <string>
#include <sstream>

#include "execute_stage.h"

#include "common/io/io.h"
#include "common/log/log.h"
#include "common/seda/timer_stage.h"
#include "common/lang/string.h"
#include "session/session.h"
#include "event/storage_event.h"
#include "event/sql_event.h"
#include "event/session_event.h"
#include "event/execution_plan_event.h"
#include "sql/executor/execution_node.h"
#include "sql/executor/tuple.h"
#include "storage/common/table.h"
#include "storage/default/default_handler.h"
#include "storage/common/condition_filter.h"
#include "storage/trx/trx.h"

using namespace common;

RC create_selection_executor(
    Trx *trx, const Selects &selects, const char *db, const char *table_name, SelectExeNode &select_node);

//! Constructor
ExecuteStage::ExecuteStage(const char *tag) : Stage(tag)
{}

//! Destructor
ExecuteStage::~ExecuteStage()
{}

//! Parse properties, instantiate a stage object
Stage *ExecuteStage::make_stage(const std::string &tag)
{
  ExecuteStage *stage = new (std::nothrow) ExecuteStage(tag.c_str());
  if (stage == nullptr) {
    LOG_ERROR("new ExecuteStage failed");
    return nullptr;
  }
  stage->set_properties();
  return stage;
}

//! Set properties for this object set in stage specific properties
bool ExecuteStage::set_properties()
{
  //  std::string stageNameStr(stageName);
  //  std::map<std::string, std::string> section = theGlobalProperties()->get(
  //    stageNameStr);
  //
  //  std::map<std::string, std::string>::iterator it;
  //
  //  std::string key;

  return true;
}

//! Initialize stage params and validate outputs
bool ExecuteStage::initialize()
{
  LOG_TRACE("Enter");

  std::list<Stage *>::iterator stgp = next_stage_list_.begin();
  default_storage_stage_ = *(stgp++);
  mem_storage_stage_ = *(stgp++);

  LOG_TRACE("Exit");
  return true;
}

//! Cleanup after disconnection
void ExecuteStage::cleanup()
{
  LOG_TRACE("Enter");

  LOG_TRACE("Exit");
}

void ExecuteStage::handle_event(StageEvent *event)
{
  LOG_TRACE("Enter\n");

  std::cout << "handle_event\n";

  handle_request(event);

  LOG_TRACE("Exit\n");
  return;
}

void ExecuteStage::callback_event(StageEvent *event, CallbackContext *context)
{
  LOG_TRACE("Enter\n");

  // here finish read all data from disk or network, but do nothing here.
  ExecutionPlanEvent *exe_event = static_cast<ExecutionPlanEvent *>(event);
  SQLStageEvent *sql_event = exe_event->sql_event();
  sql_event->done_immediate();

  LOG_TRACE("Exit\n");
  return;
}

void ExecuteStage::handle_request(common::StageEvent *event)
{
  std::cout << "do_select_2\n";
  ExecutionPlanEvent *exe_event = static_cast<ExecutionPlanEvent *>(event);
  SessionEvent *session_event = exe_event->sql_event()->session_event();
  Query *sql = exe_event->sqls();
  const char *current_db = session_event->get_client()->session->get_current_db().c_str();

  CompletionCallback *cb = new (std::nothrow) CompletionCallback(this, nullptr);
  if (cb == nullptr) {
    LOG_ERROR("Failed to new callback for ExecutionPlanEvent");
    exe_event->done_immediate();
    return;
  }
  exe_event->push_callback(cb);

  switch (sql->flag) {
    case SCF_SELECT: {  // select
    LOG_INFO("do_select\n");
      do_select(current_db, sql, exe_event->sql_event()->session_event());
      exe_event->done_immediate();
    } break;

    case SCF_INSERT:
    case SCF_UPDATE:
    case SCF_DELETE:
    case SCF_CREATE_TABLE:
    case SCF_SHOW_TABLES:
    case SCF_DESC_TABLE:
    case SCF_DROP_TABLE:
    case SCF_CREATE_INDEX:
    case SCF_DROP_INDEX:
    case SCF_LOAD_DATA: {
      StorageEvent *storage_event = new (std::nothrow) StorageEvent(exe_event);
      if (storage_event == nullptr) {
        LOG_ERROR("Failed to new StorageEvent");
        event->done_immediate();
        return;
      }

      default_storage_stage_->handle_event(storage_event);
    } break;
    case SCF_SYNC: {
      RC rc = DefaultHandler::get_default().sync();
      session_event->set_response(strrc(rc));
      exe_event->done_immediate();
    } break;
    case SCF_BEGIN: {
      session_event->get_client()->session->set_trx_multi_operation_mode(true);
      session_event->set_response(strrc(RC::SUCCESS));
      exe_event->done_immediate();
    } break;
    case SCF_COMMIT: {
      Trx *trx = session_event->get_client()->session->current_trx();
      RC rc = trx->commit();
      session_event->get_client()->session->set_trx_multi_operation_mode(false);
      session_event->set_response(strrc(rc));
      exe_event->done_immediate();
    } break;
    case SCF_ROLLBACK: {
      Trx *trx = session_event->get_client()->session->current_trx();
      RC rc = trx->rollback();
      session_event->get_client()->session->set_trx_multi_operation_mode(false);
      session_event->set_response(strrc(rc));
      exe_event->done_immediate();
    } break;
    case SCF_HELP: {
      const char *response = "show tables;\n"
                             "desc `table name`;\n"
                             "create table `table name` (`column name` `column type`, ...);\n"
                             "create index `index name` on `table` (`column`);\n"
                             "insert into `table` values(`value1`,`value2`);\n"
                             "update `table` set column=value [where `column`=`value`];\n"
                             "delete from `table` [where `column`=`value`];\n"
                             "select [ * | `columns` ] from `table`;\n";
      session_event->set_response(response);
      exe_event->done_immediate();
    } break;
    case SCF_EXIT: {
      // do nothing 
      const char *response = "Unsupported\n";
      session_event->set_response(response);
      exe_event->done_immediate();
    } break;
    default: {
      exe_event->done_immediate();
      LOG_ERROR("Unsupported command=%d\n", sql->flag);
    }
  }
}

void end_trx_if_need(Session *session, Trx *trx, bool all_right)
{
  if (!session->is_trx_multi_operation_mode()) {
    if (all_right) {
      trx->commit();
    } else {
      trx->rollback();
    }
  }
}

RC check_table_name(const Selects &selects, const char *db)
{
  // ??????from?????????????????????
  int rel_num = selects.relation_num;
  for (int i = 0; i < rel_num; i++)
  {
    Table *table = DefaultHandler::get_default().find_table(db, selects.relations[i]);
    if (nullptr == table)
    {
      LOG_WARN("No such table [%s] in db [%s]", selects.relations[i], db);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
  }

  // ??????select??????????????????from???
  for (size_t i = 0; i < selects.attr_num; i++)
  {
    const RelAttr &attr = selects.attributes[i];
    if (rel_num > 1)
    { // ??????
      // ?????????????????????"*"??????, id
      if ((nullptr == attr.relation_name) && (0 != strcmp(attr.attribute_name, "*")))
      {
        LOG_ERROR("Table name must appear.");
        return RC::SCHEMA_TABLE_NOT_EXIST;
      }
      // t1.id
      bool table_name_in_from = false;
      for (int j = 0; j < rel_num; j++)
      {
        if ((nullptr != attr.relation_name) && (0 == strcmp(attr.relation_name, selects.relations[j])))
        {
          table_name_in_from = true;
          break;
        }
      }

      // t1.*
      if ((nullptr == attr.relation_name) && (0 == strcmp(attr.attribute_name, "*")))
      {
        table_name_in_from = true;
      }

      if (table_name_in_from == false)
      {
        LOG_WARN("Table [%s] not in from", attr.relation_name);
        return RC::SCHEMA_TABLE_NOT_EXIST;
      }
    }
    else if (rel_num == 1)
    {
      if ((attr.relation_name != nullptr) && (0 != strcmp(attr.relation_name, selects.relations[0])))
      {
        LOG_WARN("Table [%s.%s] not in from", attr.relation_name, attr.attribute_name);
        return RC::SCHEMA_TABLE_NOT_EXIST;
      }
    }
  }

  // ??????where?????????????????????from???
  for (size_t i = 0; i < selects.condition_num; i++)
  {
    const Condition &condition = selects.conditions[i];
    if (rel_num == 1)
    {
      // strcmp??????????????????null?????????segmentation ??????
      for (size_t i = 0; i < selects.relation_num; i++)
      {
        if (((condition.left_is_attr == 1) && (nullptr != condition.left_attr.relation_name) && (0 != strcmp(condition.left_attr.relation_name, selects.relations[i]))) ||
            ((condition.right_is_attr == 1) && (nullptr != condition.right_attr.relation_name) && (0 != strcmp(condition.right_attr.relation_name, selects.relations[i]))))
        {
          LOG_WARN("Table name in where but not in from");
          LOG_INFO("%d - %d, i = %d", condition.left_is_attr, condition.right_is_attr, i);
          LOG_INFO("condition.left_attr.relation_name = %s, condition.right_attr.relation_name = %s, relation = %s", condition.left_attr.relation_name, condition.right_attr.relation_name, selects.relations[i]);
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
      }
    }
    else if (rel_num > 1)
    {
      if (condition.left_is_attr == 1)
      {
        // ??????????????????
        bool left_col_found = false;
        for (size_t i = 0; i < selects.relation_num; i++)
        {
          Table *table = DefaultHandler::get_default().find_table(db, selects.relations[i]);
          const TableMeta &table_meta = table->table_meta();
          const FieldMeta *field = table_meta.field(condition.left_attr.attribute_name);
          if (nullptr != field)
          {
            left_col_found = true;
            break;
          }
        }
        if (left_col_found == false)
        {
          LOG_ERROR("The column [%s] does not exist in table [%s].", condition.left_attr.attribute_name, selects.relations[i]);
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
        // LOG_INFO("condition.left_attr.relation_name: %s, %s", condition.left_attr.relation_name, condition.left_attr.attribute_name);
        // where????????????????????????????????????*
        if (nullptr == condition.left_attr.relation_name)
        {
          LOG_ERROR("Table name must appear.");
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
        bool left_is_found = false;
        for (int j = 0; j < rel_num; j++)
        {
          if (0 == strcmp(condition.left_attr.relation_name, selects.relations[j]))
          {
            left_is_found = true;
            break;
          }
        }
        if (left_is_found == false)
        {
          LOG_WARN("Table name %s appears in where but not in from", condition.left_attr.relation_name);
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
      }

      if (condition.right_is_attr == 1)
      {
        // ??????????????????
        bool right_col_found = false;
        for (size_t i = 0; i < selects.relation_num; i++)
        {
          Table *table = DefaultHandler::get_default().find_table(db, selects.relations[i]);
          const TableMeta &table_meta = table->table_meta();
          const FieldMeta *field = table_meta.field(condition.right_attr.attribute_name);
          if (nullptr != field)
          {
            right_col_found = true;
            break;
          }
        }
        if (right_col_found == false)
        {
          LOG_ERROR("The column [%s] does not exist in table [%s].", condition.right_attr.attribute_name, selects.relations[i]);
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
        // where????????????????????????????????????*
        if (nullptr == condition.right_attr.relation_name)
        {
          LOG_ERROR("Table name must appear.");
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
        bool right_is_found = false;
        for (int j = 0; j < rel_num; j++)
        {
          if (0 == strcmp(condition.right_attr.relation_name, selects.relations[j]))
          {
            right_is_found = true;
            break;
          }
        }
        if (right_is_found == false)
        {
          LOG_WARN("Table name %s appears in where but not in from", condition.right_attr.relation_name);
          return RC::SCHEMA_TABLE_NOT_EXIST;
        }
      }
    }
  }

  return RC::SUCCESS;
}

/* ??????select??????????????????????????????????????????????????? */
RC check_select(const Selects &selects, const char *db) {
  // ??????from????????????????????????
  for (size_t i = 0; i < selects.relation_num; ++i) {
    const char *name = selects.relations[i];
    std::cout << name << "\n";
    std::cout << selects.attributes[i].attribute_name << "\n";
    // printf("%d\n", name);
    if((DefaultHandler::get_default().find_table(db, name)) == nullptr) {
      // printf("%d\n", name);
      LOG_WARN("No such table [%s] in db [%s]", name, db);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }
  }

  std::cout << "form???????????????\n";

  //LOG_DEBUG("form???????????????\n");

  // ??????select????????????????????????from????????????
  for (size_t i = 0; i < selects.attr_num; ++i) {
    const RelAttr &attr = selects.attributes[i];

    bool is_star_char = !strcmp(attr.attribute_name, "*");

    std::cout << is_star_char << std::endl;

    bool table_in_from = false;
    bool filed_in_from = false;

    for (size_t j = 0; j < selects.relation_num; ++j) {
      const char *name = selects.relations[j];
      Table *table = DefaultHandler::get_default().find_table(db, name);
      const TableMeta &table_meta = table->table_meta();
      const FieldMeta *filed = table_meta.field(attr.attribute_name);
      // t.* || t.a
      if((attr.relation_name != nullptr) && (strcmp(attr.relation_name, name) == 0)) {
        table_in_from = true;
        if(filed != nullptr || is_star_char) {
          filed_in_from = true;
        }
        break;
      }
      // * || a && (selects.relation_num == 1)
      else if(attr.relation_name == nullptr && ((filed != nullptr && selects.relation_num == 1) || is_star_char)) {
        table_in_from = true;
        filed_in_from = true;
        break;
      }
    }

    if (table_in_from == false) {
      return RC::SCHEMA_TABLE_NOT_EXIST; 
    }

    if (filed_in_from == false) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
  }

  //LOG_DEBUG("select?????????????????????from????????????\n");

  // ??????where???????????????????????????????????????from????????????
  for (size_t i = 0; i < selects.condition_num; ++i) {
    const Condition &condition = selects.conditions[i];

    bool left_table_in_from = false;
    bool left_filed_in_from = false;
    bool right_table_in_from = false;
    bool right_filed_in_from = false;

    bool left_end_flag = false;
    bool right_end_flag = false;
    

    if(condition.left_is_attr == 0 && condition.right_is_attr == 0) break;

    for (size_t j = 0; j < selects.relation_num; ++j) {
      const char *name = selects.relations[j];
      Table *table = DefaultHandler::get_default().find_table(db, name);
      const TableMeta &table_meta = table->table_meta();

      if(!left_end_flag && condition.left_is_attr == 1 && condition.left_attr.relation_name != nullptr && strcmp(condition.left_attr.relation_name, name) == 0) {
        left_table_in_from = true;
        if(table_meta.field(condition.left_attr.attribute_name) != nullptr) {
          left_filed_in_from = true;
        }
        left_end_flag = true;
      }

      else if(!left_end_flag && condition.left_is_attr == 1 && condition.left_attr.relation_name == nullptr && selects.relation_num == 1) {
        if(table_meta.field(condition.left_attr.attribute_name) != nullptr) {
          left_table_in_from = true;
          left_filed_in_from = true;
          left_end_flag = true;
        }
      }

      if(!right_end_flag && condition.right_is_attr == 1 && condition.right_attr.relation_name != nullptr && strcmp(condition.right_attr.relation_name, name) == 0) {
        right_table_in_from = true;
        if(table_meta.field(condition.right_attr.attribute_name) != nullptr) {
          right_filed_in_from = true;
        }
        right_end_flag = true;
      }

      else if(!right_end_flag && condition.right_is_attr == 1 && condition.right_attr.relation_name == nullptr && selects.relation_num == 1) {
        if(table_meta.field(condition.right_attr.attribute_name) != nullptr) {
          right_table_in_from = true;
          right_filed_in_from = true;
          right_end_flag = true;
        }
      }
    }

    if(condition.left_is_attr == 0) {
      left_filed_in_from = true;
      left_table_in_from = true;
    }

    if(condition.right_is_attr == 0) {
      right_table_in_from = true;
      right_filed_in_from = true;
    }

    if(left_table_in_from == false || right_table_in_from == false) {
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    if(left_filed_in_from == false || right_filed_in_from == false) {
      return RC::SCHEMA_FIELD_NOT_EXIST;
    }
  }

  return RC::SUCCESS;
}

// ?????????????????????????????????????????????????????????????????????????????????where?????????????????????????????????????????????????????????
// ???????????????????????????. ???????????????????????????resolve????????????execution????????????????????????
RC ExecuteStage::do_select(const char *db, Query *sql, SessionEvent *session_event)
{

  RC rc = RC::SUCCESS;
  Session *session = session_event->get_client()->session;
  Trx *trx = session->current_trx();
  const Selects &selects = sql->sstr.selection;

  std::cout << "do_select" << std::endl;

  // ??????select??????????????????
  rc = check_select(selects, db);
  if (rc != RC::SUCCESS) {
    session_event->set_response("FAILURE\n");
    end_trx_if_need(session, trx, false);
    return rc;
  }

  // ??????????????????????????????????????????condition?????????????????????????????????select ????????????
  std::vector<SelectExeNode *> select_nodes;
  for (size_t i = 0; i < selects.relation_num; i++) {
    const char *table_name = selects.relations[i];
    SelectExeNode *select_node = new SelectExeNode;
    rc = create_selection_executor(trx, selects, db, table_name, *select_node);
    if (rc != RC::SUCCESS) {
      delete select_node;
      for (SelectExeNode *&tmp_node : select_nodes) {
        delete tmp_node;
      }
      end_trx_if_need(session, trx, false);
      return rc;
    }
    select_nodes.push_back(select_node);
  }

  if (select_nodes.empty()) {
    LOG_ERROR("No table given");
    end_trx_if_need(session, trx, false);
    return RC::SQL_SYNTAX;
  }

  std::vector<TupleSet> tuple_sets;
  for (SelectExeNode *&node : select_nodes) {
    TupleSet tuple_set;
    rc = node->execute(tuple_set);
    if (rc != RC::SUCCESS) {
      for (SelectExeNode *&tmp_node : select_nodes) {
        delete tmp_node;
      }
      end_trx_if_need(session, trx, false);
      return rc;
    } else {
      tuple_sets.push_back(std::move(tuple_set));
    }
  }

  std::stringstream ss;
  if (tuple_sets.size() > 1) {
    // ????????????????????????????????????join??????
  } else {
    // ???????????????????????????????????????????????????
    tuple_sets.front().print(ss);
  }

  for (SelectExeNode *&tmp_node : select_nodes) {
    delete tmp_node;
  }
  session_event->set_response(ss.str());
  end_trx_if_need(session, trx, true);
  return rc;
}

bool match_table(const Selects &selects, const char *table_name_in_condition, const char *table_name_to_match)
{
  if (table_name_in_condition != nullptr) {
    return 0 == strcmp(table_name_in_condition, table_name_to_match);
  }

  return selects.relation_num == 1;
}

static RC schema_add_field(Table *table, const char *field_name, TupleSchema &schema)
{
  const FieldMeta *field_meta = table->table_meta().field(field_name);
  if (nullptr == field_meta) {
    LOG_WARN("No such field. %s.%s", table->name(), field_name);
    return RC::SCHEMA_FIELD_MISSING;
  }

  schema.add_if_not_exists(field_meta->type(), table->name(), field_meta->name());
  return RC::SUCCESS;
}

// ??????????????????????????????????????????condition?????????????????????????????????select ????????????
RC create_selection_executor(
    Trx *trx, const Selects &selects, const char *db, const char *table_name, SelectExeNode &select_node)
{
  // ???????????????????????????Attr
  TupleSchema schema;
  Table *table = DefaultHandler::get_default().find_table(db, table_name);
  if (nullptr == table) {
    LOG_WARN("No such table [%s] in db [%s]", table_name, db);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  for (int i = selects.attr_num - 1; i >= 0; i--) {
    const RelAttr &attr = selects.attributes[i];
    if (nullptr == attr.relation_name || 0 == strcmp(table_name, attr.relation_name)) {
      if (0 == strcmp("*", attr.attribute_name)) {
        // ???????????????????????????
        TupleSchema::from_table(table, schema);
        break;  // ?????????????????????* ??????????????????????????????
      } else {
        // ???????????????????????????
        RC rc = schema_add_field(table, attr.attribute_name, schema);
        if (rc != RC::SUCCESS) {
          return rc;
        }
      }
    }
  }

  // ???????????????????????????????????????, ??????????????????????????????
  std::vector<DefaultConditionFilter *> condition_filters;
  for (size_t i = 0; i < selects.condition_num; i++) {
    const Condition &condition = selects.conditions[i];
    if ((condition.left_is_attr == 0 && condition.right_is_attr == 0) ||  // ???????????????
        (condition.left_is_attr == 1 && condition.right_is_attr == 0 &&
            match_table(selects, condition.left_attr.relation_name, table_name)) ||  // ???????????????????????????
        (condition.left_is_attr == 0 && condition.right_is_attr == 1 &&
            match_table(selects, condition.right_attr.relation_name, table_name)) ||  // ?????????????????????????????????
        (condition.left_is_attr == 1 && condition.right_is_attr == 1 &&
            match_table(selects, condition.left_attr.relation_name, table_name) &&
            match_table(selects, condition.right_attr.relation_name, table_name))  // ?????????????????????????????????????????????
    ) {
      DefaultConditionFilter *condition_filter = new DefaultConditionFilter();
      RC rc = condition_filter->init(*table, condition);
      if (rc != RC::SUCCESS) {
        delete condition_filter;
        for (DefaultConditionFilter *&filter : condition_filters) {
          delete filter;
        }
        return rc;
      }
      condition_filters.push_back(condition_filter);
    }
  }

  return select_node.init(trx, table, std::move(schema), std::move(condition_filters));
}
