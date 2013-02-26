%% -------------------------------------------------------------------
%%
%% gen_utp: uTP protocol
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
-module(gen_utp).
-behaviour(gen_server).
-author('Steve Vinoski <vinoski@ieee.org>').

-include("gen_utp_opts.hrl").

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").
-endif.

-export([start_link/0, start/0, stop/0,
         listen/1, listen/2, accept/1, accept/2, async_accept/1,
         connect/2, connect/3, connect/4,
         close/1, send/2, recv/2, recv/3,
         sockname/1, peername/1, port/1,
         setopts/2, getopts/2,
         controlling_process/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {
          port :: port()
         }).

%% driver command IDs
-define(UTP_LISTEN, 1).
-define(UTP_ACCEPT, 2).
-define(UTP_CANCEL_ACCEPT, 3).
-define(UTP_CONNECT_START, 4).
-define(UTP_CONNECT_VALIDATE, 5).
-define(UTP_CLOSE, 6).
-define(UTP_SOCKNAME, 7).
-define(UTP_PEERNAME, 8).
-define(UTP_SETOPTS, 9).
-define(UTP_GETOPTS, 10).
-define(UTP_CANCEL_SEND, 11).
-define(UTP_RECV, 12).
-define(UTP_CANCEL_RECV, 13).

-type utpstate() :: #state{}.
-type from() :: {pid(), any()}.
-type utpaddr() :: inet:ip_address() | inet:hostname().
-type utpport() :: inet:port_number().
-type utpsock() :: port().
-type utpdata() :: binary() | list().

-spec start_link() -> {ok, pid()} | ignore | {error, term()}.
start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

-spec start() -> {ok, pid()} | ignore | {error, any()}.
start() ->
    gen_server:start({local, ?MODULE}, ?MODULE, [], []).

-spec stop() -> ok.
stop() ->
    gen_server:cast(?MODULE, stop).

-spec listen(utpport()) -> {ok, utpsock()} | {error, any()}.
listen(Port) when Port >= 0, Port < 65536 ->
    listen(Port, []).

-spec listen(utpport(), gen_utp_opts:utpopts()) -> {ok, utpsock()} |
                                                   {error, any()}.
listen(Port, Options) when Port >= 0, Port < 65536 ->
    ValidOpts = gen_utp_opts:validate([{port,Port}|Options]),
    OptBin = options_to_binary(ValidOpts),
    try
        Ref = make_ref(),
        Args = term_to_binary({OptBin, term_to_binary(Ref)}),
        erlang:port_control(utpdrv, ?UTP_LISTEN, Args),
        receive
            {Ref, Result} ->
                Result
        end
    catch
        error:badarg ->
            {error, einval}
    end.

-spec accept(utpsock()) -> {ok, utpsock()} | {error, any()}.
accept(Sock) ->
    accept(Sock, infinity).

-spec accept(utpsock(), timeout()) -> {ok, utpsock()} | {error, any()}.
accept(Sock, Timeout) ->
    Ref = make_ref(),
    Args = term_to_binary(term_to_binary(Ref)),
    case async_accept(Sock, Args) of
        ok ->
            receive
                {utp_async, NewSock, Ref} ->
                    {ok, NewSock}
            after
                Timeout ->
                    try
                        erlang:port_control(Sock, ?UTP_CANCEL_ACCEPT, <<>>),
                        %% if the reply comes back while the cancel
                        %% call completes, return it
                        receive
                            {utp_async, NewSock, Ref} ->
                                {ok, NewSock}
                        after
                            0 ->
                                {error, etimedout}
                        end
                    catch
                        error:badarg ->
                            {error, einval}
                    end
            end;
        Error ->
            Error
    end.

-spec async_accept(utpsock()) -> ok | {error, any()}.
async_accept(Sock) ->
    async_accept(Sock, <<>>).

-spec async_accept(utpsock(), binary()) -> ok | {error, any()}.
async_accept(Sock, Args) ->
    try
        Result = erlang:port_control(Sock, ?UTP_ACCEPT, Args),
        binary_to_term(Result)
    catch
        error:badarg ->
            {error, einval}
    end.

-spec connect(utpaddr(), utpport()) -> {ok, utpsock()} | {error, any()}.
connect(Addr, Port) when Port > 0, Port =< 65535 ->
    connect(Addr, Port, []).

-spec connect(utpaddr(), utpport(), gen_utp_opts:utpopts()) -> {ok, utpsock()} |
                                                               {error, any()}.
connect(Addr, Port, Opts) when is_tuple(Addr), Port > 0, Port =< 65535 ->
    try inet_parse:ntoa(Addr) of
        ListAddr ->
            connect(ListAddr, Port, Opts)
    catch
        _:_ ->
            throw(badarg)
    end;
connect(Addr, Port, Opts) when Port > 0, Port =< 65535 ->
    AddrStr = case inet:getaddr(Addr, inet) of
                  {ok, AddrTuple} ->
                      inet_parse:ntoa(AddrTuple);
                  _ ->
                      case inet:getaddr(Addr, inet6) of
                          {ok, AddrTuple} ->
                              inet_parse:ntoa(AddrTuple);
                          Error ->
                              Error
                      end
              end,
    case AddrStr of
        {error, _}=Err ->
            Err;
        _ ->
            ValidOpts = gen_utp_opts:validate(Opts),
            OptBin = options_to_binary(ValidOpts),
            try
                Ref = make_ref(),
                RefBin = term_to_binary(Ref),
                Args = term_to_binary({AddrStr, Port, OptBin, RefBin}),
                erlang:port_control(utpdrv, ?UTP_CONNECT_START, Args),
                receive
                    {Ref, {ok, Sock}} ->
                        validate_connect(Sock);
                    {Ref, Result} ->
                        Result
                end
            catch
                error:badarg ->
                    {error, closed}
            end
    end.

-spec connect(utpaddr(), utpport(), gen_utp_opts:utpopts(), timeout()) ->
                     {ok, utpsock()} | {error, any()}.
connect(Addr, Port, Opts, _Timeout) when Port > 0, Port =< 65535 ->
    %% uTP doesn't give us any control over connect timeouts,
    %% so ignore the Timeout parameter for now
    connect(Addr, Port, Opts).

-spec close(utpsock()) -> ok.
close(Sock) ->
    try
        Ref = make_ref(),
        Args = term_to_binary(term_to_binary(Ref)),
        Result = erlang:port_control(Sock, ?UTP_CLOSE, Args),
        case binary_to_term(Result) of
            wait ->
                receive
                    {Ref, ok} -> ok
                end;
            ok ->
                ok
        end
    after
        true = erlang:port_close(Sock)
    end,
    ok.

-spec send(utpsock(), iodata()) -> ok | {error, any()}.
send(Sock, Data) ->
    send(Sock, Data, os:timestamp()).

-spec recv(utpsock(), non_neg_integer()) -> {ok, utpdata()} |
                                            {error, any()}.
recv(Sock, Length) ->
    recv(Sock, Length, infinity).

-spec recv(utpsock(), non_neg_integer(), timeout()) -> {ok, utpdata()} |
                                                       {error, any()}.
recv(Sock, Length, Timeout) ->
    try
        Ref = make_ref(),
        Args = term_to_binary({Length, term_to_binary(Ref)}),
        Result = erlang:port_control(Sock, ?UTP_RECV, Args),
        case binary_to_term(Result) of
            wait ->
                receive
                    {Ref, Reply} ->
                        Reply
                after
                    Timeout ->
                        erlang:port_control(Sock, ?UTP_CANCEL_RECV, <<>>),
                        %% if the reply comes back while the cancel
                        %% call completes, return it
                        receive
                            {Ref, Reply} ->
                                Reply
                        after
                            0 ->
                                {error, etimedout}
                        end
                end;
            Reply ->
                Reply
        end
    catch
        error:badarg ->
            {error, closed}
    end.

-spec sockname(utpsock()) -> {ok, {utpaddr(), utpport()}} | {error, any()}.
sockname(Sock) ->
    try
        Result = erlang:port_control(Sock, ?UTP_SOCKNAME, <<>>),
        case binary_to_term(Result) of
            {ok, {AddrStr, Port}} ->
                {ok, Addr} = inet_parse:address(AddrStr),
                {ok, {Addr, Port}};
            Error ->
                Error
        end
    catch
        error:badarg ->
            {error,einval}
    end.

-spec peername(utpsock()) -> {ok, {utpaddr(), utpport()}} | {error, any()}.
peername(Sock) ->
    try
        Result = erlang:port_control(Sock, ?UTP_PEERNAME, <<>>),
        case binary_to_term(Result) of
            {ok, {AddrStr, Port}} ->
                {ok, Addr} = inet_parse:address(AddrStr),
                {ok, {Addr, Port}};
            Error ->
                Error
        end
    catch
        error:badarg ->
            {error,einval}
    end.

-spec port(utpsock()) -> {ok, utpport()} | {error, any()}.
port(Sock) ->
    case sockname(Sock) of
        {ok, {_Addr, Port}} ->
            {ok, Port};
        Error ->
            Error
    end.

-spec setopts(utpsock(), gen_utp_opts:utpopts()) -> ok | {error, any()}.
setopts(Sock, Opts) when is_list(Opts) ->
    ValidOpts = gen_utp_opts:validate(Opts),
    OptCheck = fun(Opt) -> Opt =/= undefined end,
    case lists:any(OptCheck, [ValidOpts#utp_options.ip,
                              ValidOpts#utp_options.port,
                              ValidOpts#utp_options.family]) of
        true ->
            erlang:error(badarg, Opts);
        false ->
            OptBin = options_to_binary(ValidOpts),
            Args = term_to_binary(OptBin),
            try
                Result = erlang:port_control(Sock, ?UTP_SETOPTS, Args),
                binary_to_term(Result)
            catch
                error:badarg ->
                    {error, closed}
            end
    end.

-spec getopts(utpsock(), gen_utp_opts:utpgetoptnames()) ->
                     {ok, gen_utp_opts:utpopts()} | {error, any()}.
getopts(Sock, OptNames) when is_list(OptNames) ->
    case gen_utp_opts:validate_names(OptNames) of
        {ok, OptNameBin} ->
            Args = term_to_binary(OptNameBin),
            try
                Result = erlang:port_control(Sock, ?UTP_GETOPTS, Args),
                binary_to_term(Result)
            catch
                error:badarg ->
                    {error, closed}
            end;
        Error ->
            Error
    end.

-spec controlling_process(utpsock(), pid()) -> ok | {error, any()}.
controlling_process(Sock, NewOwner) ->
    case erlang:port_info(Sock, connected) of
        {connected, NewOwner} ->
            ok;
        {connected, Pid} when Pid =/= self() ->
            {error, not_owner};
        undefined ->
            {error, einval};
        _ ->
            try erlang:port_connect(Sock, NewOwner) of
                true ->
                    unlink(Sock),
                    ok
            catch
                error:Reason ->
                    {error, Reason}
            end
    end.

%% gen_server functions

-spec init([]) -> ignore | {ok, utpstate()} | {stop, any()}.
init([]) ->
    process_flag(trap_exit, true),
    Shlib = "utpdrv",
    PrivDir = case code:priv_dir(?MODULE) of
                  {error, bad_name} ->
                      EbinDir = filename:dirname(code:which(?MODULE)),
                      AppPath = filename:dirname(EbinDir),
                      filename:join(AppPath, "priv");
                  Path ->
                      Path
              end,
    LoadResult = case erl_ddll:load_driver(PrivDir, Shlib) of
                     ok -> ok;
                     {error, already_loaded} -> ok;
                     {error, LoadError} ->
                         LoadErrorStr = erl_ddll:format_error(LoadError),
                         EStr = lists:flatten(
                                  io_lib:format("could not load driver ~s: ~p",
                                                [Shlib, LoadErrorStr])),
                         {stop, EStr}
                 end,
    case LoadResult of
        ok ->
            Port = erlang:open_port({spawn, Shlib}, [binary]),
            register(utpdrv, Port),
            {ok, #state{port=Port}};
        Error ->
            Error
    end.

-spec handle_call(any(), from(), utpstate()) -> {reply, any(), utpstate()}.
handle_call(_Request, _From, State) ->
    {reply, ok, State}.

-spec handle_cast(any(), utpstate()) -> {noreply, utpstate()} |
                                        {stop, any(), utpstate()}.
handle_cast(stop, State) ->
    {stop, normal, State};
handle_cast(_Msg, State) ->
    {noreply, State}.

-spec handle_info(any(), utpstate()) -> {noreply, utpstate()}.
handle_info(_Info, State) ->
    {noreply, State}.

-spec terminate(any(), utpstate()) -> ok.
terminate(_Reason, #state{port=Port}) ->
    unregister(utpdrv),
    erlang:port_close(Port),
    ok.

-spec code_change(any(), utpstate(), any()) -> {ok, utpstate()}.
code_change(_OldVsn, State, _Extra) ->
    {ok, State}.


%% Internal functions

%%
%% The send/3 function takes the socket and data to be sent, as well as a
%% timestamp of when the operation was started. The send timeout is set
%% when the socket is created and is stored in the driver. This function
%% uses the starting timestamp to help enforce any send timeout.
%%
%% If the socket is writable, the driver takes the data and makes sure it
%% gets sent. If it's not writable, the driver sends a message telling the
%% sender to wait. If a send timeout is set on the socket, it includes that
%% timeout in the message. The process then awaits a message from the
%% driver telling it to retry. If a timeout occurs before the retry message
%% arrives, an error is returned to the caller. When a retry arrives, check
%% the start timestamp to ensure a timeout hasn't occurred, and if not try
%% the send operation again.
%%
-spec send(utpsock(), iodata(), erlang:timestamp()) -> ok | {error, any()}.
send(Sock, Data, Start) ->
    try
        true = erlang:port_command(Sock, Data),
        receive
            {utp_reply, Sock, wait} ->
                receive
                    {utp_reply, Sock, retry} ->
                        send(Sock, Data, Start)
                end;
            {utp_reply, Sock, {wait, Timeout}} ->
                Current = os:timestamp(),
                TDiff = timer:now_diff(Current, Start) div 1000,
                case TDiff >= Timeout of
                    true ->
                        {error, etimedout};
                    false ->
                        {StartMega, StartSec, StartMicro} = Start,
                        Done = {StartMega, StartSec, Timeout*1000+StartMicro},
                        Wait = timer:now_diff(Done, Current) div 1000,
                        receive
                            {utp_reply, Sock, retry} ->
                                send(Sock, Data, Start)
                        after
                            Wait ->
                                erlang:port_control(Sock,?UTP_CANCEL_SEND,<<>>),
                                %% if the reply comes back while the cancel
                                %% call completes, return it
                                receive
                                    {utp_reply, Sock, ok} ->
                                        ok;
                                    {utp_reply, Sock, _} ->
                                        {error, etimedout}
                                after
                                    0 ->
                                        {error, etimedout}
                                end
                        end
                end;
            {utp_reply, Sock, Result} ->
                Result
        end
    catch
        error:badarg ->
            {error, einval}
    end.

-spec validate_connect(utpsock()) -> {ok, utpsock()} | {error, any()}.
validate_connect(Sock) ->
    Ref = make_ref(),
    Args = term_to_binary(term_to_binary(Ref)),
    Bin = erlang:port_control(Sock, ?UTP_CONNECT_VALIDATE, Args),
    case binary_to_term(Bin) of
        ok ->
            {ok, Sock};
        wait ->
            receive
                {Ref, ok} ->
                    {ok, Sock};
                {Ref, Err} ->
                    close(Sock),
                    Err
            end;
        Error ->
            erlang:port_close(Sock),
            Error
    end.

-spec options_to_binary(#utp_options{}) -> binary().
options_to_binary(UtpOpts) ->
    list_to_binary([
                    case UtpOpts#utp_options.mode of
                        undefined ->
                            <<>>;
                        list ->
                            <<?UTP_LIST_OPT:8>>;
                        binary ->
                            <<?UTP_BINARY_OPT:8>>
                    end,
                    case UtpOpts#utp_options.family of
                        undefined ->
                            <<>>;
                        inet ->
                            <<>>;
                        inet6 ->
                            <<?UTP_INET6_OPT:8>>
                    end,
                    case UtpOpts#utp_options.ip of
                        undefined ->
                            <<>>;
                        AddrStr ->
                            [?UTP_IP_OPT, AddrStr, 0]
                    end,
                    case UtpOpts#utp_options.port of
                        undefined ->
                            <<>>;
                        Port ->
                            <<?UTP_PORT_OPT:8, Port:16/big>>
                    end,
                    case UtpOpts#utp_options.send_tmout of
                        undefined ->
                            <<>>;
                        infinity ->
                            <<?UTP_SEND_TMOUT_INFINITE_OPT:8>>;
                        Tm ->
                            <<?UTP_SEND_TMOUT_OPT:8, Tm:32/big>>
                    end,
                    case UtpOpts#utp_options.active of
                        undefined ->
                            <<>>;
                        false ->
                            <<?UTP_ACTIVE_OPT:8, ?UTP_ACTIVE_FALSE:8>>;
                        once ->
                            <<?UTP_ACTIVE_OPT:8, ?UTP_ACTIVE_ONCE:8>>;
                        true ->
                            <<?UTP_ACTIVE_OPT:8, ?UTP_ACTIVE_TRUE:8>>
                    end,
                    case UtpOpts#utp_options.packet of
                        undefined ->
                            <<>>;
                        Val ->
                            <<?UTP_PACKET_OPT:8, Val:8>>
                    end,
                    case UtpOpts#utp_options.header of
                        undefined ->
                            <<>>;
                        HdrSize ->
                            <<?UTP_HEADER_OPT:8, HdrSize:16/big>>
                    end,
                    case UtpOpts#utp_options.sndbuf of
                        undefined ->
                            <<>>;
                        SndBuf ->
                            <<?UTP_SNDBUF_OPT:8, SndBuf:32/big>>
                    end,
                    case UtpOpts#utp_options.recbuf of
                        undefined ->
                            <<>>;
                        RecBuf ->
                            <<?UTP_RECBUF_OPT:8, RecBuf:32/big>>
                    end
                   ]).
