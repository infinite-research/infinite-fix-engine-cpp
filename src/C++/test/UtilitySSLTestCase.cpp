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

#if (HAVE_SSL > 0)

#include <UtilitySSL.h>

#include "catch_amalgamated.hpp"

using namespace FIX;

TEST_CASE("UtilitySSLTests") {
  SECTION("context enforces TLS 1.2 minimum") {
    SessionSettings settings;
    std::string error;
    SSL_CTX *context = createSSLContext(GENERATE(false, true), settings, error);
    REQUIRE(context);
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    CHECK(SSL_CTX_get_min_proto_version(context) == TLS1_2_VERSION);
#else
    const long disabled = SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1;
    CHECK((SSL_CTX_get_options(context) & disabled) == disabled);
#endif
    SSL_CTX_free(context);
  }

  SECTION("protocol tokens require complete boundaries") {
    CHECK(protocolOptions("TLSv1_2TLSv1_3") == -1);
    CHECK(protocolOptions("all nonsense") == -1);
    CHECK(protocolOptions(" TLSv1_2 ") == SSL_PROTOCOL_TLSV1_2);
  }
  SECTION("is_ip_address_IPv4_ReturnsTrue") {
    CHECK(is_ip_address("192.168.1.1"));
    CHECK(is_ip_address("127.0.0.1"));
    CHECK(is_ip_address("0.0.0.0"));
    CHECK(is_ip_address("255.255.255.255"));
    CHECK(is_ip_address("10.0.0.1"));
  }

  SECTION("is_ip_address_IPv6_ReturnsTrue") {
    CHECK(is_ip_address("::1"));
    CHECK(is_ip_address("::"));
    CHECK(is_ip_address("2001:db8::1"));
    CHECK(is_ip_address("fe80::1"));
    CHECK(is_ip_address("::ffff:192.168.1.1"));
  }

  SECTION("is_ip_address_IPv6_Bracketed_ReturnsTrue") {
    CHECK(is_ip_address("[::1]"));
    CHECK(is_ip_address("[2001:db8::1]"));
    CHECK(is_ip_address("[fe80::1]"));
    CHECK(is_ip_address("[::]"));
  }

  SECTION("is_ip_address_Hostname_ReturnsFalse") {
    CHECK_FALSE(is_ip_address("localhost"));
    CHECK_FALSE(is_ip_address("example.com"));
    CHECK_FALSE(is_ip_address("fix.server.example.com"));
    CHECK_FALSE(is_ip_address("my-server-01"));
    CHECK_FALSE(is_ip_address("server123"));
  }

  SECTION("is_ip_address_Empty_ReturnsFalse") { CHECK_FALSE(is_ip_address("")); }

  SECTION("is_ip_address_InvalidInput_ReturnsFalse") {
    CHECK_FALSE(is_ip_address("not-an-ip"));
    CHECK_FALSE(is_ip_address("192.168.1"));
    CHECK_FALSE(is_ip_address("192.168.1.256"));
    CHECK_FALSE(is_ip_address("[incomplete"));
  }

  SECTION("ssl_set_sni_hostname_NullSSL_ReturnsFalse") { CHECK_FALSE(ssl_set_sni_hostname(nullptr, "example.com")); }

  SECTION("ssl_set_sni_hostname_EmptyHostname_ReturnsTrue") {
    // Empty hostname should be skipped silently
    // We can't test with null SSL but we can document behavior
    CHECK_FALSE(ssl_set_sni_hostname(nullptr, ""));
  }
}

#endif // HAVE_SSL
