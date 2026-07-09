%% Contract fixture for module attributes, records, maps, funs, and receives.
-module(rotide_contract).
-export([run/1, handle/2]).
-include_lib("kernel/include/file.hrl").
-define(DEFAULT_TIMEOUT, 1000).
-record(state, {count = 0, owner}).

-spec run(pid()) -> ok | {error, term()}.
run(Pid) ->
    State0 = #state{owner = self()},
    Fun = fun(Value) -> handle(Value, State0) end,
    Pid ! {self(), <<"ping">>},
    receive
        {Pid, #{status := ok, value := Value}} ->
            Fun(Value);
        {Pid, Error = {error, _Reason}} ->
            Error
    after ?DEFAULT_TIMEOUT ->
            {error, timeout}
    end.

handle(Value, #state{count = Count} = State) when is_integer(Value) ->
    Next = State#state{count = Count + Value},
    case Next#state.count of
        N when N > 10 ->
            ok;
        _ ->
            {error, too_small}
    end.
