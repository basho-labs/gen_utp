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
-type utpportopt() :: {port,gen_utp:utpport()}.
-type utpmodeopt() :: {mode,utpmode()} | utpmode().
-type utpsendopt() :: {send_timeout,utptimeout()}.
-type utpactive() :: once | boolean().
-type utpactiveopt() :: {active, utpactive()}.
-type utppacketsize() :: raw | 0 | 1 | 2 | 4.
-type utppacketopt() :: {packet, utppacketsize()}.
-type utpheadersize() :: pos_integer().
-type utpheaderopt() :: {header, utpheadersize()}.
-type utpopt() :: utpipopt() | utpportopt() | utpmodeopt() |
                  utpfamily() | utpsendopt() | utpactiveopt() |
                  utppacketopt() | utpheaderopt().
-type utpopts() :: [utpopt()].
-type utpgetoptname() :: active | mode | send_timeout.
-type utpgetoptnames() :: [utpgetoptname()].


-spec validate(utpopts()) -> #utp_options{}.
validate(Opts) when is_list(Opts) ->
    validate(Opts, #utp_options{}).

-spec validate_names(utpgetoptnames()) -> {ok, binary()} | {error, any()}.
validate_names(OptNames) when is_list(OptNames) ->
    Result = lists:foldl(fun(_, {error, _}=Error) ->
                                 Error;
                            (active, Bin) ->
                                 <<Bin/binary, ?UTP_ACTIVE_OPT:8>>;
                            (mode, Bin) ->
                                 <<Bin/binary, ?UTP_MODE_OPT:8>>;
                            (send_timeout, Bin) ->
                                 <<Bin/binary, ?UTP_SEND_TMOUT_OPT:8>>;
                            (packet, Bin) ->
                                 <<Bin/binary, ?UTP_PACKET_OPT:8>>;
                            (header, Bin) ->
                                 <<Bin/binary, ?UTP_HEADER_OPT:8>>;
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
validate([{packet,raw}|Opts], UtpOpts) ->
    validate([{packet,0}|Opts], UtpOpts);
validate([{packet,P}|Opts], UtpOpts)
  when P == 0; P == 1; P == 2; P == 4 ->
    validate(Opts, UtpOpts#utp_options{packet=P});
validate([{packet,_}=Packet|_], _) ->
    erlang:error(badarg, [Packet]);
validate([{header,Sz}|Opts], UtpOpts)
  when is_integer(Sz), Sz > 0, Sz < 65536 ->
    validate(Opts, UtpOpts#utp_options{header=Sz});
validate([{header,_}=Hdr|_], _) ->
    erlang:error(badarg, [Hdr]);
validate([], UtpOpts) ->
    case UtpOpts#utp_options.header of
        undefined ->
            UtpOpts;
        Size ->
            %% {header,Size} valid only in binary mode
            case UtpOpts#utp_options.mode of
                binary ->
                    UtpOpts;
                _ ->
                    erlang:error(badarg, [{mode,list},{header,Size}])
            end
    end.

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


-ifdef(TEST).

validate_test() ->
    #utp_options{ip=IP1,family=Family1} = validate([{ip,"::"},inet6]),
    ?assertMatch("::",IP1),
    ?assertMatch(inet6,Family1),
    #utp_options{ip=IP2,family=Family2} = validate([{ip,"127.0.0.1"},inet]),
    ?assertMatch("127.0.0.1",IP2),
    ?assertMatch(inet,Family2),
    #utp_options{family=Family3} = validate([inet]),
    ?assertMatch(inet,Family3),
    #utp_options{family=Family4} = validate([inet6]),
    ?assertMatch(inet6,Family4),
    ?assertMatch(#utp_options{packet=0}, validate([{packet,raw}])),
    ?assertMatch(#utp_options{packet=0}, validate([{packet,0}])),
    ?assertMatch(#utp_options{packet=1}, validate([{packet,1}])),
    ?assertMatch(#utp_options{packet=2}, validate([{packet,2}])),
    ?assertMatch(#utp_options{packet=4}, validate([{packet,4}])),
    ?assertMatch(#utp_options{header=1}, validate([binary,{header,1}])),

    ?assertException(error, badarg, validate([{mode,bin}])),
    ?assertException(error, badarg, validate([{port,65536}])),
    ?assertException(error, badarg, validate([{ip,{127,0,0,1}},inet6])),
    ?assertException(error, badarg, validate([{ip,"::"},inet])),
    ?assertException(error, badarg, validate([{send_timeout,0}])),
    ?assertException(error, badarg, validate([{active,never}])),
    ?assertException(error, badarg, validate([{ip,{1,2,3,4,5}}])),
    ?assertException(error, badarg, validate([{ip,"1.2.3.4.5"}])),
    ?assertException(error, badarg, validate([{packet,3}])),
    ?assertException(error, badarg, validate([{header,1}])),
    ok.

validate_names_test() ->
    OkOpts = [active,mode,send_timeout,packet,header],
    ?assertMatch({ok,_}, validate_names(OkOpts)),
    ?assertMatch({error, einval}, validate_names([list])),
    ?assertMatch({error, einval}, validate_names([binary])),
    ?assertMatch({error, einval}, validate_names([binary,list])),
    ?assertMatch({error, einval}, validate_names([inet])),
    ?assertMatch({error, einval}, validate_names([inet6])),
    ?assertMatch({error, einval}, validate_names([ip])),
    ?assertMatch({error, einval}, validate_names([ifaddr])),
    ?assertMatch({error, einval}, validate_names([port])),
    ok.

-endif.
