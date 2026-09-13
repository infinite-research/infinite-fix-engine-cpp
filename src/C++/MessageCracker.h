/* -*- C++ -*- */

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

#ifndef FIX_MESSAGECRACKER_H
#define FIX_MESSAGECRACKER_H

#include "Session.h"
#include "Values.h"
#include "fix40/MessageCracker.h"
#include "fix41/MessageCracker.h"
#include "fix42/MessageCracker.h"
#include "fix43/MessageCracker.h"
#include "fix44/MessageCracker.h"
#include "fix50/MessageCracker.h"
#include "fix50sp1/MessageCracker.h"
#include "fix50sp2/MessageCracker.h"
#include "fixt11/MessageCracker.h"

namespace FIX {
/** Takes in a generic Message and produces an object that represents
 *  its specific version and message type.
 */
class MessageCracker : public FIX40::MessageCracker,
                       public FIX41::MessageCracker,
                       public FIX42::MessageCracker,
                       public FIX43::MessageCracker,
                       public FIX44::MessageCracker,
                       public FIX50::MessageCracker,
                       public FIX50SP1::MessageCracker,
                       public FIX50SP2::MessageCracker,
                       public FIXT11::MessageCracker {
public:
  void crack(const Message &message, const SessionID &sessionID) {
    const auto beginString = message.getHeader().getField<BeginString>();

    crack(message, sessionID, beginString);
  }

  void crack(const Message &message, const SessionID &sessionID, const BeginString &beginString) {
    if (beginString == BeginString_FIX40) {
      FIX40::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX41) {
      FIX41::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX42) {
      FIX42::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX43) {
      FIX43::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX44) {
      FIX44::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIXT11) {
      if (message.isAdmin()) {
        FIXT11::MessageCracker::crack(message, sessionID);
      } else {
        ApplVerID applVerID;
        if (!message.getHeader().getFieldIfSet(applVerID)) {
          Session *pSession = Session::lookupSession(sessionID);
          applVerID = pSession->getSenderDefaultApplVerID();
        }

        crack(message, sessionID, applVerID);
      }
    }
  }

  void crack(const Message &message, const SessionID &sessionID, const ApplVerID &applVerID) {
    if (applVerID == ApplVerID_FIX40) {
      FIX40::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX41) {
      FIX41::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX42) {
      FIX42::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX43) {
      FIX43::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX44) {
      FIX44::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50) {
      FIX50::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50_SP1) {
      FIX50SP1::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50_SP2) {
      FIX50SP2::MessageCracker::crack(message, sessionID);
    }
  }

  void crack(Message &message, const SessionID &sessionID) {
    const auto beginString = message.getHeader().getField<BeginString>();

    crack(message, sessionID, beginString);
  }

  void crack(Message &message, const SessionID &sessionID, const BeginString &beginString) {
    if (beginString == BeginString_FIX40) {
      FIX40::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX41) {
      FIX41::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX42) {
      FIX42::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX43) {
      FIX43::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIX44) {
      FIX44::MessageCracker::crack(message, sessionID);
    } else if (beginString == BeginString_FIXT11) {
      if (message.isAdmin()) {
        FIXT11::MessageCracker::crack(message, sessionID);
      } else {
        ApplVerID applVerID;
        if (!message.getHeader().getFieldIfSet(applVerID)) {
          Session *pSession = Session::lookupSession(sessionID);
          applVerID = pSession->getSenderDefaultApplVerID();
        }

        crack(message, sessionID, applVerID);
      }
    }
  }

  void crack(Message &message, const SessionID &sessionID, const ApplVerID &applVerID) {
    if (applVerID == ApplVerID_FIX40) {
      FIX40::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX41) {
      FIX41::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX42) {
      FIX42::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX43) {
      FIX43::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX44) {
      FIX44::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50) {
      FIX50::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50_SP1) {
      FIX50SP1::MessageCracker::crack(message, sessionID);
    }
    if (applVerID == ApplVerID_FIX50_SP2) {
      FIX50SP2::MessageCracker::crack(message, sessionID);
    }
  }
};
} // namespace FIX

#endif // FIX_MESSAGECRACKER_H
