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

#include <DataDictionaryProvider.h>
#include <Fields.h>
#include <Values.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("DataDictionaryProviderTests") {
  SECTION("getApplicationDataDictionary_DataDictionaryNotSet") {
    DataDictionaryProvider object;
    DataDictionary expected;

    ApplVerID id;
    DataDictionary actual = object.getApplicationDataDictionary(id);

    CHECK(expected.getVersion() == actual.getVersion());
    CHECK(expected.getOrderedFields() == actual.getOrderedFields());
  }

  SECTION("copies preserve shared dictionary identity") {
    const BeginString beginString(BeginString_FIX42);
    const ApplVerID applVerID(ApplVerID_FIX50);
    auto transportDictionary = std::make_shared<DataDictionary>();
    transportDictionary->setVersion(BeginString_FIX42);
    auto applicationDictionary = std::make_shared<DataDictionary>();
    applicationDictionary->setVersion(BeginString_FIX50);
    DataDictionaryProvider original;
    original.addTransportDataDictionary(beginString, transportDictionary);
    original.addApplicationDataDictionary(applVerID, applicationDictionary);

    DataDictionaryProvider copied(original);
    DataDictionaryProvider assigned;
    assigned = original;

    CHECK(&copied.getSessionDataDictionary(beginString) == transportDictionary.get());
    CHECK(&assigned.getSessionDataDictionary(beginString) == transportDictionary.get());
    CHECK(&copied.getApplicationDataDictionary(applVerID) == applicationDictionary.get());
    CHECK(&assigned.getApplicationDataDictionary(applVerID) == applicationDictionary.get());
    CHECK(copied.getSessionDataDictionary(beginString).getVersion() == BeginString_FIX42);
    CHECK(copied.getApplicationDataDictionary(applVerID).getVersion() == BeginString_FIX50);
  }
}
