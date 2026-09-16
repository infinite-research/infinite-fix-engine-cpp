#!/bin/sh
sleep 1

QUICKFIX_TEST_PORT=54321

"${QUICKFIX_TEST_SRCDIR:-.}/runut.sh"
RESULT=$?
if [ $RESULT != 0 ]
then 
  exit $RESULT
fi

"${QUICKFIX_TEST_SRCDIR:-.}/runat.sh" nonthreaded "$QUICKFIX_TEST_PORT"
RESULT=$?
if [ $RESULT != 0 ]
then 
  exit $RESULT
fi

"${QUICKFIX_TEST_SRCDIR:-.}/runat.sh" threaded "$((QUICKFIX_TEST_PORT + 1))"
RESULT=$?
if [ $RESULT != 0 ]
then
  exit $RESULT
fi

sleep 1
