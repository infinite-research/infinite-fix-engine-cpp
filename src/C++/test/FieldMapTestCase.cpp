/****************************************************************************
** Copyright (c) 2001-2014
**
** This file is part of the QuickFIX FIX Engine
**
** This file may be distributed under the terms of the quickfixengine.org
** license as defined by quickfixengine.org and appearing in the file
** LICENSE included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.quickfixengine.org/LICENSE for licensing information.
**
** Contact ask@quickfixengine.org if any conditions of this licensing are
** not clear to you.
**
****************************************************************************/

#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#include "stdafx.h"
#else
#include "config.h"
#endif

#include <FieldMap.h>
#include <Message.h>
#include <utility>
#include <vector>

#include "catch_amalgamated.hpp"

using namespace FIX;

namespace {
template <typename Map> Symbol getSymbol(const Map &map) { return FIELD_GET_REF(map, Symbol); }
} // namespace

TEST_CASE("FieldMapTests") {
  SECTION("typed retrieval returns independent real fields") {
    FieldMap fields;
    fields.setField(Symbol("MSFT"));
    fields.setField(OrderQty(12.5));
    fields.setField(SendingTime(UtcTimeStamp(12, 34, 56, 8, 9, 2026)));
    const auto &symbol = fields.getField<Symbol>();
    const auto &quantity = fields.getField<OrderQty>();
    const auto &time = fields.getField<SendingTime>();
    CHECK(typeid(symbol) == typeid(Symbol));
    CHECK(typeid(quantity) == typeid(OrderQty));
    CHECK(typeid(time) == typeid(SendingTime));
    CHECK(symbol.getValue() == "MSFT");
    CHECK(quantity.getValue() == 12.5);
    CHECK(time.getValue() == UtcTimeStamp(12, 34, 56, 8, 9, 2026));
    fields.setField(Symbol("IBM"));
    fields.setField(OrderQty(1));
    fields.setField(SendingTime(UtcTimeStamp(0, 0, 0, 1, 1, 2000)));
    CHECK(symbol.getValue() == "MSFT");
    CHECK(quantity.getValue() == 12.5);
    CHECK(time.getValue() == UtcTimeStamp(12, 34, 56, 8, 9, 2026));
    CHECK_THROWS_AS(fields.getField<ClOrdID>(), FieldNotFound);
    CHECK_FALSE(fields.getFieldOptional<ClOrdID>().has_value());
    CHECK(fields.getFieldOptional<Symbol>()->getValue() == "IBM");
    const auto &macroSymbol = FIELD_GET_REF(fields, Symbol);
    const auto &dependentSymbol = getSymbol(fields);
    fields.setField(Symbol("ORCL"));
    CHECK(macroSymbol.getValue() == "IBM");
    CHECK(dependentSymbol.getValue() == "IBM");
  }

  SECTION("move assignment releases previously owned groups") {
    struct TrackedGroup : FieldMap {
      explicit TrackedGroup(bool &destroyed)
          : destroyed(destroyed) {}
      ~TrackedGroup() override { destroyed = true; }
      bool &destroyed;
    };

    bool destroyed = false;
    FieldMap destination;
    destination.addGroupPtr(1, new TrackedGroup(destroyed), false);
    FieldMap source;
    FieldMap sourceGroup;
    source.addGroup(2, sourceGroup, false);

    destination = std::move(source);

    CHECK(destroyed);
    CHECK(destination.groupCount(2) == 1);
  }

  SECTION("setMessageOrder") {
    int order[] = {1, 2, 3, 0}; // '0' is used to signify the end of array passed to FieldMap()
    FieldMap fieldMap(order);
    fieldMap.setField(3, "account");
    fieldMap.setField(1, "adv_id");
    fieldMap.setField(2, "adv_ref_id");

    int pos1 = 0, pos2 = 0, pos3 = 0;
    int iterationCount = 0;
    for (FieldMap::iterator itr = fieldMap.begin(); itr != fieldMap.end(); itr++, iterationCount++) {
      if (iterationCount == 0) {
        pos1 = itr->getTag();
      } else if (iterationCount == 1) {
        pos2 = itr->getTag();
      } else if (iterationCount == 2) {
        pos3 = itr->getTag();
      }
    }

    CHECK(1 == pos1);
    CHECK(2 == pos2);
    CHECK(3 == pos3);
  }

  SECTION("addGroupPtr_nullptr") {
    FieldMap fieldMap;
    fieldMap.addGroupPtr(1, nullptr);
    CHECK(0U == fieldMap.groupCount(0));
  }

  SECTION("removeGroup_allGroupsWithSameTag") {
    FieldMap fieldMap;
    FieldMap group1;
    group1.setField(2, "field2");

    FieldMap group2;
    group2.setField(2, "field2");

    fieldMap.addGroup(1, group1);
    fieldMap.addGroup(1, group2);
    CHECK(2ul == fieldMap.groupCount(1));

    fieldMap.removeGroup(2, 1);
    fieldMap.removeGroup(1, 1);
    CHECK(0ul == fieldMap.groupCount(1));
  }

  SECTION("removeGroup_whenCountFieldIsRemoved") {
    FieldMap fieldMap;
    FieldMap group1;
    group1.setField(2, "field2");

    FieldMap group2;
    group2.setField(2, "field2");

    fieldMap.addGroup(1, group1);
    fieldMap.addGroup(1, group2);
    CHECK(2ul == fieldMap.groupCount(1));

    fieldMap.removeField(1);
    CHECK(0ul == fieldMap.groupCount(1));
  }

  SECTION("hasGroup_groupExists") {
    FieldMap fieldMap;
    FieldMap group;
    fieldMap.addGroup(1, group);

    CHECK(fieldMap.hasGroup(1));
  }

  SECTION("hasGroup_groupDoesNotExist") {
    FieldMap fieldMap;
    FieldMap group;
    fieldMap.addGroup(1, group);

    CHECK(!fieldMap.hasGroup(2));
  }

  SECTION("totalFields") {
    FieldMap fieldMap;
    fieldMap.setField(1, "field1");
    fieldMap.setField(2, "field2");
    fieldMap.setField(3, "field3");

    FieldMap group1;
    group1.setField(4, "field4");
    fieldMap.addGroup(10, group1);
    FieldMap group2;
    group2.setField(5, "field5");
    group2.setField(6, "field6");
    fieldMap.addGroup(20, group2);

    CHECK(8ul == fieldMap.totalFields());
  }

  SECTION("setField_16FieldsAlreadyExist_fieldSet") {
    FieldMap fieldMap;
    fieldMap.setField(1, "field1");
    fieldMap.setField(2, "field2");
    fieldMap.setField(3, "field3");
    fieldMap.setField(4, "field4");
    fieldMap.setField(5, "field5");
    fieldMap.setField(6, "field6");

    fieldMap.setField(7, "field7");
    fieldMap.setField(8, "field8");
    fieldMap.setField(9, "field9");
    fieldMap.setField(10, "field10");
    fieldMap.setField(11, "field11");
    fieldMap.setField(12, "field12");
    fieldMap.setField(13, "field13");
    fieldMap.setField(14, "field14");
    fieldMap.setField(15, "field15");
    fieldMap.setField(16, "field16");
    fieldMap.setField(17, "field17");
    fieldMap.setField(18, "field18");

    FieldBase expectedTag18(18, "field18_new");

    fieldMap.setField(expectedTag18);

    FieldBase actualTag18(18, "");
    fieldMap.getFieldIfSet(actualTag18);

    CHECK(18 == actualTag18.getTag());
    CHECK("field18_new" == actualTag18.getString());
  }

  SECTION("getFieldOptional") {
    FieldMap fieldMap;
    fieldMap.setField(FIX::BeginSeqNo(42));
    auto beginSeqNo = fieldMap.getFieldOptional<FIX::BeginSeqNo>();
    CHECK(beginSeqNo.has_value());
    CHECK(beginSeqNo.value() == 42);
    auto endSeqNo = fieldMap.getFieldOptional<FIX::EndSeqNo>();
    CHECK(!endSeqNo.has_value());
  }
}
