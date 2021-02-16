vaktari
=====

Semantic Erlang monitors - associate data to a monitor and get it back
in its DOWN message.

Here we explore monitors that are easier to work with in particular
situations.

### Example

Run `rebar3 shell` to get an Erlang shell with `vaktari` ready to use.

```
1> Pid = spawn(fun () -> timer:sleep(300 * 1000) end).
<0.143.0>

2> Monitor = vaktari:monitor(process, Pid, {user_id, <<"12345">>}).
#Ref<0.734555226.3953262593.217377>

3> exit(Pid, kill).
true

4> flush().
Shell got {'DOWN',#Ref<0.734555226.3953262593.217377>,process,<0.143.0>,
                  undefined,
                  {user_id,<<"12345">>}}
```

`{user_id, <<"12345">>}` would then allow you to identify which process
terminated without having to laboriously keep track of its monitor in
your state.

This is of particular relevance when a process is responsible for
monitoring many other processes and those processes are associated
with additional identifiers other than a `pid` or its monitor.

**This library is purely experimental and you shouldn't use it.**

Current limitations:
- only local processes can be monitored;
- there's no way of providing the actual termination reason due to the current
  `erl_nif` API (which is why it's `undefined` in the example above);
- according to the docs, one of the reasons `enif_demonitor_process` may fail
  is if the monitor is "just about to be triggered by a concurrent thread";
  this makes me suspect that `vaktari:demonitor(Monitor, [flush])` may not be
  fully reliable.
