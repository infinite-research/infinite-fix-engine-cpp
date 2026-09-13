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

#include <Settings.h>
#include <sstream>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("SettingsTests") {
  SECTION("readCompleteLines") {
    const auto length = GENERATE(16, 1022, 1023, 1024, 4096);
    const std::string newline = GENERATE(std::string("\n"), std::string("\r\n"));
    const bool finalNewline = GENERATE(false, true);
    CAPTURE(length, newline, finalNewline);
    const std::string value(length, 'x');
    std::istringstream input(
        "[FIRST]" + newline + "Value=" + value + newline + "[LAST]" + newline + "Value=" + value
        + (finalNewline ? newline : ""));
    Settings object;

    CHECK_NOTHROW(input >> object);
    REQUIRE(object.get("FIRST").size() == 1);
    CHECK(object.get("FIRST")[0].has("Value"));
    if (object.get("FIRST")[0].has("Value")) {
      CHECK(object.get("FIRST")[0].getString("Value") == value);
    }
    CHECK(object.get("LAST").size() == 1);
    if (object.get("LAST").size() == 1) {
      CHECK(object.get("LAST")[0].getString("Value") == value);
    }
  }

  SECTION("failedReadPreservesExistingSettings") {
    struct FailingBuffer : std::stringbuf {
      FailingBuffer(const std::string &configuration)
          : std::stringbuf(configuration) {
        setg(eback(), eback(), eback() + configuration.find("unfinished") + 3);
      }
      int_type underflow() override { throw std::ios_base::failure("read failed"); }
    } buffer("[NEW]\nValue=new\n[PARTIAL]\nValue=unfinished\n[UNREAD]\nValue=unread\n");
    std::istream input(&buffer);
    Settings object;
    std::istringstream initial("[OLD]\nValue=old\n");
    initial >> object;

    CHECK_THROWS_AS(input >> object, ConfigError);
    CHECK(input.bad());
    REQUIRE(object.get("OLD").size() == 1);
    CHECK(object.get("OLD")[0].getString("Value") == "old");
    CHECK(object.get("NEW").empty());
    CHECK(object.get("PARTIAL").empty());
    CHECK(object.get("UNREAD").empty());
  }

  SECTION("rejectFailedStreamState") {
    const auto state = GENERATE(std::ios::failbit, std::ios::badbit | std::ios::eofbit);
    std::istringstream input("[NEW]\nValue=new\n");
    input.setstate(state);
    Settings object;
    CHECK_THROWS_AS(input >> object, ConfigError);
    CHECK(object.get("NEW").empty());
  }

  SECTION("successfulReadAppendsSections") {
    Settings object;
    std::istringstream first("[SECTION]\nValue=first\n");
    std::istringstream second("Ignored=before section\n[SECTION]\nValue=second");
    first >> object;
    second >> object;
    const auto sections = object.get("SECTION");
    REQUIRE(sections.size() == 2);
    CHECK(sections[0].getString("Value") == "first");
    CHECK(sections[1].getString("Value") == "second");
    CHECK_FALSE(sections[0].has("Ignored"));
  }

  SECTION("readFromIstream") {
    Settings object;
    std::string configuration = "[FOO]\nbar=24\nbaz=moo\n\n"
                                "[OREN]\nNero=TW\n#Nero=IGNOREME\n"
                                "# this is a comment\n"
                                "#[OREN]\n#Nero=IGNOREME\n"
                                "[OREN]\nISLD=Nero\nSTUFF=./\\:\n"
                                "[NERO]\nWINDIR=D:\\This Is\\A-Directory\\ok\\\n"
                                "\nWINFILE=D:\\Program Files\\Tomcat 4.1\\webapps\\mek\\WEB-INF\\HTTPtoFIX.cfg\n"
                                "UNIXDIR=/This Is/A Directory/ok/\n"
                                "stripspace=last spaces stripped  \n"
                                "#Nero=IGNOREME";

    std::istringstream input(configuration);

    input >> object;

    Settings::Sections none = object.get("NONE");
    CHECK(0U == none.size());

    Settings::Sections foo = object.get("FOO");
    CHECK(1U == foo.size());
    CHECK(24 == foo[0].getInt("bar"));
    CHECK("moo" == foo[0].getString("baz"));
    CHECK(2lu == foo[0].size());

    Settings::Sections oren = object.get("OREN");
    CHECK(2lu == oren.size());
    CHECK(1lu == oren[0].size());
    CHECK("TW" == oren[0].getString("Nero"));
    CHECK(2lu == oren[1].size());
    CHECK("Nero" == oren[1].getString("ISLD"));
    CHECK("./\\:" == oren[1].getString("STUFF"));

    Settings::Sections nero = object.get("NERO");
    CHECK(1lu == nero.size());
    CHECK(4lu == nero[0].size());
    CHECK("D:\\This Is\\A-Directory\\ok\\" == nero[0].getString("WINDIR"));
    CHECK("/This Is/A Directory/ok/" == nero[0].getString("UNIXDIR"));
    CHECK("D:\\Program Files\\Tomcat 4.1\\webapps\\mek\\WEB-INF\\HTTPtoFIX.cfg" == nero[0].getString("WINFILE"));
    CHECK("last spaces stripped" == nero[0].getString("stripspace"));
  }
}
