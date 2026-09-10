# Set $fail_spawn before sourcing; select "SocketAcceptor thread spawn failure".
# QUICKFIX_TEST_THREADED / _TLS select transport; _WORKER uses block(), _ASYNC uses start().
set pagination off
set confirm off
set breakpoint pending on
set debuginfod enabled off
set $spawn_count = 0
break FIX::thread_spawn(void* (*)(void*), void*, unsigned long&)
commands
  silent
  set $spawn_count = $spawn_count + 1
  if $spawn_count == $fail_spawn
    return (int)0
  end
  continue
end
run
quit $_exitcode
