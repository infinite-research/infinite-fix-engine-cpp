<!--
*****************************************************************************
  Copyright (c) 2001-2014

  This file is part of the QuickFIX FIX Engine

  This file may be distributed under the terms of the quickfixengine.org
  license as defined by quickfixengine.org and appearing in the file
  LICENSE included in the packaging of this file.

  This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
  WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.

  See http://www.quickfixengine.org/LICENSE for licensing information.

  Contact ask@quickfixengine.org if any conditions of this licensing are
  not clear to you.
*****************************************************************************
-->

<!--
  Generates fixNN/MessageCracker.cpp: the out-of-line crack() definitions for the
  class declared by MessageCracker.xsl. Only this translation unit includes every
  message header of the version, which keeps MessageCracker.h cheap to include.
-->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
 <xsl:output  method="text" encoding="UTF-8"/>

 <xsl:template match="text()"/>

 <xsl:variable name="namespace">
  <xsl:value-of select="//fix/@type"/><xsl:value-of select="//fix/@major"/><xsl:value-of select="//fix/@minor"/>
  <xsl:if test="//fix/@servicepack!='0'">SP<xsl:value-of select="//fix/@servicepack"/></xsl:if>
 </xsl:variable>

 <xsl:template match="/">/* -*- C++ -*- */
<xsl:copy-of select="document('COPYRIGHT.xml')"/>
#ifdef _MSC_VER
#include "stdafx.h"
#else
#include "config.h"
#endif

#include "MessageCracker.h"

#include &lt;utility&gt;

<xsl:for-each select="//fix/messages/message">#include "<xsl:value-of select="@name"/>.h"
</xsl:for-each>
namespace <xsl:value-of select="$namespace"/>
{
  namespace
  {
    /// Deliver a genuine T to the const callback.
    template &lt;typename T&gt;
    void crackConst( MessageCracker&amp; cracker, const FIX::Message&amp; message, const FIX::SessionID&amp; sessionID )
    {
      if( const T* typed = dynamic_cast&lt;const T*&gt;( &amp;message ) )
      {
        cracker.onMessage( *typed, sessionID );
        return;
      }
      // Viewing an object that is not a T as a T is undefined behaviour, so one copy is unavoidable here.
      cracker.onMessage( T( message ), sessionID );
    }

    /// Deliver a genuine T to the mutable callback without copying the caller's content.
    template &lt;typename T&gt;
    void crackMutable( MessageCracker&amp; cracker, FIX::Message&amp; message, const FIX::SessionID&amp; sessionID )
    {
      if( T* typed = dynamic_cast&lt;T*&gt;( &amp;message ) )
      {
        cracker.onMessage( *typed, sessionID );
        return;
      }
      // Lend the caller's content to an empty T and move it back on return and on exception.
      T typed{ FIX::Message() };
      FIX::Message&amp; content = typed;
      content = std::move( message );
      try
      {
        cracker.onMessage( typed, sessionID );
      }
      catch( ... )
      {
        message = std::move( content );
        throw;
      }
      message = std::move( content );
    }
  }

  void MessageCracker::crack( const Message&amp; message,
                              const FIX::SessionID&amp; sessionID )
  {
    crack( static_cast&lt;const FIX::Message&amp;&gt;( message ), sessionID );
  }

  void MessageCracker::crack( const FIX::Message&amp; message,
                              const FIX::SessionID&amp; sessionID )
  {
    const std::string&amp; msgTypeValue
      = message.getHeader().getField( FIX::FIELD::MsgType );
<xsl:for-each select="//fix/messages/message">
    if( msgTypeValue == "<xsl:value-of select="@msgtype"/>" )
      return crackConst&lt;<xsl:value-of select="@name"/>&gt;( *this, message, sessionID );</xsl:for-each>

    return crackConst&lt;Message&gt;( *this, message, sessionID );
  }

  void MessageCracker::crack( Message&amp; message,
                              const FIX::SessionID&amp; sessionID )
  {
    crack( static_cast&lt;FIX::Message&amp;&gt;( message ), sessionID );
  }

  void MessageCracker::crack( FIX::Message&amp; message,
                              const FIX::SessionID&amp; sessionID )
  {
    const std::string&amp; msgTypeValue
      = message.getHeader().getField( FIX::FIELD::MsgType );
<xsl:for-each select="//fix/messages/message">
    if( msgTypeValue == "<xsl:value-of select="@msgtype"/>" )
      return crackMutable&lt;<xsl:value-of select="@name"/>&gt;( *this, message, sessionID );</xsl:for-each>

    return crackMutable&lt;Message&gt;( *this, message, sessionID );
  }
}
</xsl:template>

</xsl:stylesheet>
