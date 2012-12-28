%% -------------------------------------------------------------------
%%
%% gen_utp_opts: socket options for uTP protocol
%%
%% Copyright (c) 2012 Basho Technologies, Inc. All Rights Reserved.
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

-export([validate/1]).

-type utpmsg() :: list | binary.
-type utptimeout() :: pos_integer() | infinity.
-type utpfamily() :: inet | inet6.
-type utpipopt() :: {ip,gen_utp:utpaddr()} | {ifaddr,gen_utp:utpaddr()}.
-type utpfdopt() :: {fd,non_neg_integer()}.
-type utpportopt() :: {port,gen_utp:utpport()}.
-type utpmsgopt() :: {mode,utpmsg()} | utpmsg().
-type utpsendopt() :: {send_timeout,utptimeout()}.
-type utpopt() :: utpipopt() | utpfdopt() | utpportopt() | utpmsgopt() |
                  utpfamily() | utpsendopt().
-type utpopts() :: [utpopt()].


-spec validate(utpopts()) -> #utp_options{}.
validate(Opts) ->
    validate(Opts, #utp_options{}).

-spec validate(utpopts(), #utp_options{}) -> #utp_options{}.
validate([{mode,Mode}|Opts], UtpOpts) when Mode =:= listen; Mode =:= binary ->
    validate(Opts, UtpOpts#utp_options{delivery=Mode});
validate([{mode,_}=Mode|_], _) ->
    erlang:error(badarg, [Mode]);
validate([binary|Opts], UtpOpts) ->
    validate(Opts, UtpOpts#utp_options{delivery=binary});
validate([list|Opts], UtpOpts) ->
    validate(Opts, UtpOpts#utp_options{delivery=list});
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
