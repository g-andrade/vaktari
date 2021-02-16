%% Copyright (c) 2021 Guilherme Andrade
%%
%% Permission is hereby granted, free of charge, to any person obtaining a
%% copy  of this software and associated documentation files (the "Software"),
%% to deal in the Software without restriction, including without limitation
%% the rights to use, copy, modify, merge, publish, distribute, sublicense,
%% and/or sell copies of the Software, and to permit persons to whom the
%% Software is furnished to do so, subject to the following conditions:
%%
%% The above copyright notice and this permission notice shall be included in
%% all copies or substantial portions of the Software.
%%
%% THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
%% IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
%% FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
%% AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
%% LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
%% FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
%% DEALINGS IN THE SOFTWARE.

-module(vaktari).

%% ------------------------------------------------------------------
%% API Function Exports
%% ------------------------------------------------------------------

-export(
   [monitor/3,
    demonitor/1,
    demonitor/2
   ]).

%% ------------------------------------------------------------------
%% Record and Type Definitions
%% ------------------------------------------------------------------

-type monitor() :: term().
-export_type([monitor/0]).

%% ------------------------------------------------------------------
%% API Function Definitions
%% ------------------------------------------------------------------

-spec monitor(Type, Item, DownData) -> Monitor
        when Type :: process,
             Item :: pid(),
             DownData :: term(),
             Monitor :: monitor().
monitor(process, Pid, DownData) ->
    vaktari_nif:monitor(Pid, DownData).

-spec demonitor(Monitor) -> true
        when Monitor :: monitor().
demonitor(Monitor) ->
    _ = vaktari_nif:demonitor(Monitor),
    true.

-spec demonitor(Monitor, Options) -> boolean()
        when Monitor :: monitor(),
             Options :: [Option],
             Option :: flush | info.
demonitor(Monitor, Options) ->
    {Flush, Info} = parse_demonitor_options(Options),
    (vaktari_nif:demonitor(Monitor)
     orelse case Flush of
                true ->
                    receive
                        {'DOWN', Monitor, _, _, _, _} ->
                            true
                    after
                        0 ->
                            not Info
                    end;
                false ->
                    not Info
            end).

%% ------------------------------------------------------------------
%% Internal Function Definitions
%% ------------------------------------------------------------------

parse_demonitor_options(Options) ->
    parse_demonitor_options_recur(Options, _Flush = false, _Info = false).

parse_demonitor_options_recur([flush | Next], _Flush, Info) ->
    parse_demonitor_options_recur(Next, true, Info);
parse_demonitor_options_recur([info | Next], Flush, _Info) ->
    parse_demonitor_options_recur(Next, Flush, true);
parse_demonitor_options_recur([], Flush, Info) ->
    {Flush, Info};
parse_demonitor_options_recur(_, _, _) ->
    error(badarg).
