# Set $fail_spawn before sourcing; select "SocketAcceptor thread spawn failure".
# QUICKFIX_TEST_THREADED / _TLS select transport; _WORKER uses block(), _ASYNC uses start().
set pagination off
set confirm off
set breakpoint pending on
set debuginfod enabled off
set $spawn_count = 0
set $join_count = 0
break FIX::thread_join(unsigned long)
commands
  silent
  set $join_count = $join_count + 1
  continue
end
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
if $_exitcode == 0 && $join_count != $spawn_count - 1
  printf "Unreaped spawned thread: %d successful spawns, %d joins\n", $spawn_count - 1, $join_count
  quit 1
end
quit $_exitcode
