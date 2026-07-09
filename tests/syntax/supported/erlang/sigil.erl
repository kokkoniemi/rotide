%% Sigils highlight as strings; bare brackets stay structural (tuples/lists/maps).
-module(rotide_sigil).
-export([render/1]).

render(Name) ->
    Bin = ~b"hello",
    Verbatim = ~S{world},
    Pairs = #{name => Name, greeting => Bin},
    [Verbatim, {ok, Pairs}].
