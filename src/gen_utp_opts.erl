%% -------------------------------------------------------------------
%%
%% gen_utp_opts: socket options for uTP protocol
%%
%% Copyright (c) 2012-2013 Basho Technologies, Inc. All Rights Reserved.
%%
%% This file is provided to you under the Apache License,
%% Version 2.0 (the "License"); you may not use this file
%% except in compliance with the License.  You may obtain
%% a copy of the License at
%%
%%   http://www.apache.org/licenses/LICENSE-2.0
%%
%% Unless required by applicable law or agreed to in writing,
%% software distributed under the License is distributed on an
%% "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
%% KIND, either express or implied.  See the License for the
%% specific language governing permissions and limitations
%% under the License.
%%
%% -------------------------------------------------------------------
-module(gen_utp_opts).
-author('Steve Vinoski <vinoski@ieee.org>').

-include("gen_utp_opts.hrl").

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").
-endif.

-export([validate/1, validate_names/1]).

-type utpmode() :: list | binary.
-type utptimeout() :: pos_integer() | infinity.
-type utpfamily() :: inet | inet6.
-type utpipopt() :: {ip,gen_utp:utpaddr()} | {ifaddr,gen_utp:utpaddr()}.
-type utpfdopt() :: {fd,non_neg_integer()}.
-type utpportopt() :: {port,gen_utp:utpport()}.
-type utpmodeopt() :: {mode,utpmode()} | utpmode().
-type utpsendopt() :: {send_timeout,utptimeout()}.
-type utpactive() :: once | boolean().
-type utpactiveopt() :: {active, utpactive()}.
-type utpasyncaccept() :: {async_accept, boolean()}.
-type utpopt() :: utpipopt() | utpfdopt() | utpportopt() | utpmodeopt() |
                  utpfamily() | utpsendopt() | utpactiveopt() |
                  utpasyncaccept().
-type utpopts() :: [utpopt()].
-type utpgetoptname() :: active | mode | send_timeout | async_accept.
-type utpgetoptnames() :: [utpgetoptname()].


-spec validate(utpopts()) -> #utp_options{}.
validate(Opts) when is_list(Opts) ->
    validate(Opts, #utp_options{}).

-spec validate_names(utpgetoptnames()) -> {ok, binary()} | {error, any()}.
validate_names(OptNames) when is_list(OptNames) ->
    Result = lists:foldl(fun(_, {error, _}=Error) ->
                                 Error;
                            (active, Bin) ->
                                 <<Bin/binary, ?UTP_ACTIVE:8>>;
                            (mode, Bin) ->
                                 <<Bin/binary, ?UTP_MODE:8>>;
                            (send_timeout, Bin) ->
                                 <<Bin/binary, ?UTP_SEND_TMOUT:8>>;
                            (_, _) ->
                                 {error, einval}
                         end, <<>>, OptNames),
    case Result of
        {error, _}=Error ->
            Error;
        BinOpts ->
            {ok, BinOpts}
    end.

%% Internal functions

-spec validate(utpopts(), #utp_options{}) -> #utp_options{}.
validate([{mode,Mode}|Opts], UtpOpts) when Mode =:= list; Mode =:= binary ->
    validate(Opts, UtpOpts#utp_options{mode=Mode});
validate([{mode,_}=Mode|_], _) ->
    erlang:error(badarg, [Mode]);
validate([binary|Opts], UtpOpts) ->
    validate(Opts, UtpOpts#utp_options{mode=binary});
validate([list|Opts], UtpOpts) ->
    validate(Opts, UtpOpts#utp_options{mode=list});
validate([{ip,IpAddr}|Opts], UtpOpts) when is_tuple(IpAddr) ->
    validate(Opts, validate_ipaddr(IpAddr, UtpOpts));
validate([{ip,IpAddr}|Opts], UtpOpts) when is_list(IpAddr) ->
    validate(Opts, validate_ipaddr(IpAddr, UtpOpts));
validate([{ifaddr,IpAddr}|Opts], UtpOpts) when is_tuple(IpAddr) ->
    validate(Opts, validate_ipaddr(IpAddr, UtpOpts));
validate([{ifaddr,IpAddr}|Opts], UtpOpts) when is_list(IpAddr) ->
    validate(Opts, validate_ipaddr(IpAddr, UtpOpts));
validate([{port,Port}|Opts], UtpOpts)
  when is_integer(Port), Port >= 0, Port < 65536 ->
    validate(Opts, UtpOpts#utp_options{port=Port});
validate([{port,_}=Port|_], _) ->
    erlang:error(badarg, [Port]);
validate([{fd,Fd}|Opts], UtpOpts) when is_integer(Fd), Fd >= 0 ->
    validate(Opts, UtpOpts#utp_options{fd=Fd});
validate([{fd,_}=Fd|_], _) ->
    erlang:error(badarg, [Fd]);
validate([inet|Opts], UtpOpts) ->
    case UtpOpts#utp_options.ip of
        undefined ->
            validate(Opts, UtpOpts#utp_options{family=inet});
        _ ->
            case UtpOpts#utp_options.family of
                inet ->
                    validate(Opts, UtpOpts);
                _ ->
                    erlang:error(badarg)
            end
    end;
validate([inet6|Opts], UtpOpts) ->
    case UtpOpts#utp_options.ip of
        undefined ->
            validate(Opts, UtpOpts#utp_options{family=inet6});
        _ ->
            case UtpOpts#utp_options.family of
                inet6 ->
                    validate(Opts, UtpOpts);
                _ ->
                    erlang:error(badarg)
            end
    end;
validate([{send_timeout,Tm}|Opts], UtpOpts)
  when Tm =:= infinity; is_integer(Tm), Tm > 0 ->
    validate(Opts, UtpOpts#utp_options{send_tmout=Tm});
validate([{send_timeout,_}=ST|_], _) ->
    erlang:error(badarg, [ST]);
validate([{active,Active}|Opts], UtpOpts)
  when is_boolean(Active); Active =:= once ->
    validate(Opts, UtpOpts#utp_options{active=Active});
validate([{active,_}=Active|_], _) ->
    erlang:error(badarg, [Active]);
validate([{async_accept,AsyncAccept}|Opts], UtpOpts)
  when is_boolean(AsyncAccept) ->
    validate(Opts, UtpOpts#utp_options{async_accept=AsyncAccept});
validate([{async_accept,_}=Async|_], _) ->
    erlang:error(badarg, [Async]);
validate([], UtpOpts) ->
    UtpOpts.

validate_ipaddr(IpAddr, UtpOpts) when is_tuple(IpAddr) ->
    try inet_parse:ntoa(IpAddr) of
        ListAddr ->
            validate_ipaddr(ListAddr, UtpOpts)
    catch
        _:_ ->
            erlang:error(badarg, [IpAddr])
    end;
validate_ipaddr(IpAddr, UtpOpts) when is_list(IpAddr) ->
    case inet:getaddr(IpAddr, inet) of
        {ok, AddrTuple} ->
            UtpOpts#utp_options{family=inet, ip=inet_parse:ntoa(AddrTuple)};
        _ ->
            case inet:getaddr(IpAddr, inet6) of
                {ok, AddrTuple} ->
                    UtpOpts#utp_options{family=inet6,
                                        ip=inet_parse:ntoa(AddrTuple)};
                _Error ->
                    erlang:error(badarg, [IpAddr])
            end
    end.
