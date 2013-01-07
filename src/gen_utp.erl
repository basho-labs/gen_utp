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
         listen/1, listen/2,
         connect/2, connect/3,
         close/1, send/2, recv/2, recv/3,
         sockname/1, peername/1,
         setopts/2, getopts/2,
         controlling_process/2]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {
          port :: port()
         }).

%% driver command IDs
-define(UTP_LISTEN, 1).
-define(UTP_CONNECT_START, 2).
-define(UTP_CONNECT_VALIDATE, 3).
-define(UTP_CLOSE, 4).
-define(UTP_SOCKNAME, 5).
-define(UTP_PEERNAME, 6).
-define(UTP_SETOPTS, 7).
-define(UTP_GETOPTS, 8).
-define(UTP_CANCEL_SEND, 9).
-define(UTP_RECV, 10).
-define(UTP_CANCEL_RECV, 11).

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
        _:Reason ->
            {error, Reason}
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
            try
                ValidOpts = gen_utp_opts:validate(Opts),
                OptBin = options_to_binary(ValidOpts),
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
                _:Reason ->
                    {error, Reason}
            end
    end.

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
                        erlang:port_control(Sock,?UTP_CANCEL_RECV,<<>>),
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
        _:Reason ->
            {error, Reason}
    end.

-spec sockname(utpsock()) -> {ok, {utpaddr(), utpport()}} | {error, any()}.
sockname(Sock) ->
    Result = erlang:port_control(Sock, ?UTP_SOCKNAME, <<>>),
    case binary_to_term(Result) of
        {ok, {AddrStr, Port}} ->
            {ok, Addr} = inet_parse:address(AddrStr),
            {ok, {Addr, Port}};
        Error ->
            Error
    end.

-spec peername(utpsock()) -> {ok, {utpaddr(), utpport()}} | {error, any()}.
peername(Sock) ->
    Result = erlang:port_control(Sock, ?UTP_PEERNAME, <<>>),
    case binary_to_term(Result) of
        {ok, {AddrStr, Port}} ->
            {ok, Addr} = inet_parse:address(AddrStr),
            {ok, {Addr, Port}};
        Error ->
            Error
    end.

-spec setopts(utpsock(), gen_utp_opts:utpopts()) -> ok | {error, any()}.
setopts(Sock, Opts) when is_list(Opts) ->
    ValidOpts = gen_utp_opts:validate(Opts),
    OptCheck = fun(Opt) -> Opt =/= undefined end,
    case lists:any(OptCheck, [ValidOpts#utp_options.ip,
                              ValidOpts#utp_options.port,
                              ValidOpts#utp_options.fd,
                              ValidOpts#utp_options.family]) of
        true ->
            erlang:error(badarg, Opts);
        false ->
            OptBin = options_to_binary(ValidOpts),
            Args = term_to_binary(OptBin),
            Result = erlang:port_control(Sock, ?UTP_SETOPTS, Args),
            binary_to_term(Result)
    end.

-spec getopts(utpsock(), gen_utp_opts:utpgetoptnames()) ->
                     {ok, gen_utp_opts:utpopts()} | {error, any()}.
getopts(Sock, OptNames) when is_list(OptNames) ->
    case gen_utp_opts:validate_names(OptNames) of
        {ok, OptNameBin} ->
            Args = term_to_binary(OptNameBin),
            Result = erlang:port_control(Sock, ?UTP_GETOPTS, Args),
            binary_to_term(Result);
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
handle_info({close, DrvPort}, State) ->
    try
        io:format("**** closing port ~p~n", [DrvPort]),
        erlang:port_close(DrvPort)
    catch
        _:_ ->
            ok
    end,
    {noreply, State};
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
        _:Reason ->
            {error, Reason}
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
                            <<>>;
                        binary ->
                            <<?UTP_BINARY:8>>
                    end,
                    case UtpOpts#utp_options.family of
                        undefined ->
                            <<>>;
                        inet ->
                            <<>>;
                        inet6 ->
                            <<?UTP_INET6:8>>
                    end,
                    case UtpOpts#utp_options.ip of
                        undefined ->
                            <<>>;
                        AddrStr ->
                            [?UTP_IP, AddrStr, 0]
                    end,
                    case UtpOpts#utp_options.port of
                        undefined ->
                            <<>>;
                        Port ->
                            <<?UTP_PORT:8, Port:16/big>>
                    end,
                    case UtpOpts#utp_options.fd of
                        undefined ->
                            <<>>;
                        Fd ->
                            <<?UTP_FD:8, Fd:32/big>>
                    end,
                    case UtpOpts#utp_options.send_tmout of
                        infinity ->
                            <<>>;
                        undefined ->
                            <<>>;
                        Tm ->
                            <<?UTP_SEND_TMOUT:8, Tm:32/big>>
                    end,
                    case UtpOpts#utp_options.active of
                        undefined ->
                            <<>>;
                        false ->
                            <<>>;
                        once ->
                            <<?UTP_ACTIVE:8, ?UTP_ACTIVE_ONCE:8>>;
                        true ->
                            <<?UTP_ACTIVE:8, ?UTP_ACTIVE_TRUE:8>>
                    end
                   ]).


%% Tests functions

-ifdef(TEST).

setup() ->
    gen_utp:start().

cleanup(_) ->
    gen_utp:stop(),
    Check = fun(F) ->
                    case whereis(gen_utp) of
                        undefined -> ok;
                        _ -> F(F)
                    end
            end,
    Check(Check).

port_number_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun (_) ->
             {"uTP port number range test",
              ?_test(
                 begin
                     Error = {error, function_clause},
                     Error = try gen_utp:listen(-1)
                             catch C1:R1 -> {C1,R1} end,
                     Error = try gen_utp:listen(65536)
                             catch C2:R2 -> {C2,R2} end,
                     Error = try gen_utp:connect("localhost", -1)
                             catch C3:R3 -> {C3,R3} end,
                     Error = try gen_utp:connect("localhost", 0)
                             catch C4:R4 -> {C4,R4} end,
                     Error = try gen_utp:connect("localhost", 65536)
                             catch C5:R5 -> {C5,R5} end
                 end)}
     end}.

listen_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun (_) ->
             {inorder,
              [{"uTP simple listen test",
                ?_test(
                   begin
                       Ports = length(erlang:ports()),
                       {ok, LSock} = gen_utp:listen(0),
                       true = erlang:is_port(LSock),
                       Ports = length(erlang:ports()) - 1,
                       Self = self(),
                       {connected, Self} = erlang:port_info(LSock, connected),
                       {error, enotconn} = gen_utp:send(LSock, <<"send">>),
                       {ok, {Addr, Port}} = gen_utp:sockname(LSock),
                       true = is_tuple(Addr),
                       true = is_number(Port),
                       {error, enotconn} = gen_utp:peername(LSock),
                       ok = gen_utp:close(LSock),
                       undefined = erlang:port_info(LSock),
                       Ports = length(erlang:ports()),
                       ok
                   end)},
               {"uTP two listen test",
                ?_test(
                   begin
                       {ok, LSock1} = gen_utp:listen(0),
                       {ok, {_, Port}} = gen_utp:sockname(LSock1),
                       ok = gen_utp:close(LSock1),
                       {ok, LSock2} = gen_utp:listen(Port),
                       ok = gen_utp:close(LSock2)
                   end)},
               {"uTP specific interface listen test",
                ?_test(
                   begin
                       {ok, LSock1} = gen_utp:listen(0, [{ip, "127.0.0.1"}]),
                       {ok, {{127,0,0,1}, _}} = gen_utp:sockname(LSock1),
                       ok = gen_utp:close(LSock1),
                       {ok, LSock2} = gen_utp:listen(0, [{ifaddr, "127.0.0.1"}]),
                       {ok, {{127,0,0,1}, _}} = gen_utp:sockname(LSock2),
                       ok = gen_utp:close(LSock2)
                   end)}
              ]}
     end}.

client_timeout_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun (_) ->
             {timeout, 15,
              [{"uTP client timeout test",
                ?_test(
                   begin
                       {ok, LSock} = gen_utp:listen(0),
                       {ok, {_, Port}} = gen_utp:sockname(LSock),
                       ok = gen_utp:close(LSock),
                       {error, etimedout} = gen_utp:connect("localhost", Port)
                   end)}
              ]}
     end}.

client_server_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun (_) ->
             {inorder,
              [{"uTP simple connect test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = simple_connect_server(Self, Ref)
                                  end),
                       ok = simple_connect_client(Ref)
                   end)},
               {"uTP simple send binary test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = simple_send_server(Self, Ref)
                                  end),
                       ok = simple_send_client(Ref, binary)
                   end)},
               {"uTP simple send list test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = simple_send_server(Self, Ref)
                                  end),
                       ok = simple_send_client(Ref, list)
                   end)},
               {"uTP two clients test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = two_client_server(Self, Ref)
                                  end),
                       ok = two_clients(Ref)
                   end)},
               {"uTP client large send",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       Bin = list_to_binary(lists:duplicate(1000000, $A)),
                       spawn_link(fun() ->
                                          ok = large_send_server(Self, Ref, Bin)
                                  end),
                       ok = large_send_client(Ref, Bin)
                   end)},
               {"uTP two servers test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = two_servers(Self, Ref)
                                  end),
                       ok = two_server_client(Ref)
                   end)}
              ]}
     end}.

simple_connect_server(Client, Ref) ->
    Opts = [{active,true}, {mode,binary}],
    {ok, LSock} = gen_utp:listen(0, Opts),
    Client ! gen_utp:sockname(LSock),
    receive
        {utp_async, Sock, {Addr, Port}} ->
            true = is_port(Sock),
            true = is_tuple(Addr),
            true = is_number(Port),
            ok = gen_utp:close(Sock)
    after
        3000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    Client ! {done, Ref},
    ok.

simple_connect_client(Ref) ->
    receive
        {ok, {_, LPort}} ->
            Opts = [{active,true}, {mode,binary}],
            {ok, Sock} = gen_utp:connect("127.0.0.1", LPort, Opts),
            true = erlang:is_port(Sock),
            Self = self(),
            {connected, Self} = erlang:port_info(Sock, connected),
            ok = gen_utp:close(Sock),
            receive
                {done, Ref} -> ok
            after
                5000 -> exit(failure)
            end
    after
        5000 -> exit(failure)
    end,
    ok.

simple_send_server(Client, Ref) ->
    Opts = [{active,true}, {mode,binary}],
    {ok, LSock} = gen_utp:listen(0, Opts),
    Client ! gen_utp:sockname(LSock),
    receive
        {utp_async, Sock, {_Addr, _Port}} ->
            receive
                {utp, Sock, <<"simple send client">>} ->
                    ok = gen_utp:send(Sock, <<"simple send server">>);
                Error ->
                    exit(Error)
            after
                5000 -> exit(failure)
            end,
            ok = gen_utp:close(Sock)
    after
        5000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    Client ! {done, Ref},
    ok.

simple_send_client(Ref, Mode) ->
    receive
        {ok, {_, LPort}} ->
            Opts = [Mode,{active,true}],
            {ok, Sock} = gen_utp:connect("127.0.0.1", LPort, Opts),
            ok = gen_utp:send(Sock, <<"simple send client">>),
            receive
                {utp, Sock, Reply} ->
                    case Mode of
                        binary ->
                            ?assertMatch(Reply, <<"simple send server">>);
                        list ->
                            ?assertMatch(Reply, "simple send server")
                    end,
                    receive
                        {done, Ref} -> ok
                    after
                        5000 -> exit(failure)
                    end
            after
                5000 -> exit(failure)
            end,
            ok = gen_utp:close(Sock)
    after
        5000 -> exit(failure)
    end,
    ok.

two_client_server(Client, Ref) ->
    Opts = [{active,true}, {mode,binary}],
    {ok, LSock} = gen_utp:listen(0, Opts),
    Client ! gen_utp:sockname(LSock),
    receive
        {utp_async, Sock1, {_Addr1, _Port1}} ->
            receive
                {utp, Sock1, <<"client1">>} ->
                    ok = gen_utp:send(Sock1, <<"client1">>),
                    receive
                        {utp_async, Sock2, {_Addr2, _Port2}} ->
                            receive
                                {utp, Sock2, <<"client2">>} ->
                                    ok = gen_utp:send(Sock2, <<"client2">>),
                                    ok = gen_utp:close(Sock2);
                                Error ->
                                    exit(Error)
                            after
                                5000 -> exit(failure)
                            end
                    after
                        5000 -> exit(failure)
                    end,
                    ok = gen_utp:close(Sock1);
                Error ->
                    exit(Error)
            after
                5000 -> exit(failure)
            end
    after
        5000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    Client ! {done, Ref},
    ok.

two_clients(Ref) ->
    receive
        {ok, {_, LPort}} ->
            Opts = [{active,true}, {mode,binary}],
            {ok, Sock1} = gen_utp:connect("127.0.0.1", LPort, Opts),
            ok = gen_utp:send(Sock1, <<"client1">>),
            {ok, Sock2} = gen_utp:connect("127.0.0.1", LPort, Opts),
            receive
                {utp, Sock1, <<"client1">>} ->
                    ok = gen_utp:send(Sock2, <<"client2">>),
                    receive
                        {utp, Sock2, <<"client2">>} ->
                            receive
                                {done, Ref} -> ok
                            after
                                5000 -> exit(failure)
                            end
                    after
                        5000 -> exit(failure)
                    end
            after
                5000 -> exit(failure)
            end,
            ok = gen_utp:close(Sock1),
            ok = gen_utp:close(Sock2)
    after
        5000 -> exit(failure)
    end,
    ok.

large_send_server(Client, Ref, Bin) ->
    Opts = [{active,true}, {mode,binary}],
    {ok, LSock} = gen_utp:listen(0, Opts),
    Client ! gen_utp:sockname(LSock),
    receive
        {utp_async, Sock, {_Addr, _Port}} ->
            Bin = large_receive(Sock, byte_size(Bin)),
            ok = gen_utp:send(Sock, <<"large send server">>),
            ok = gen_utp:close(Sock)
    after
        5000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    Client ! {done, Ref},
    ok.

large_receive(Sock, Size) ->
    large_receive(Sock, Size, 0, <<>>).
large_receive(_, Size, Size, Bin) ->
    Bin;
large_receive(Sock, Size, Count, Bin) ->
    receive
        {utp, Sock, Data} ->
            NBin = <<Bin/binary, Data/binary>>,
            large_receive(Sock, Size, Count+byte_size(Data), NBin);
        Error ->
            exit(Error)
    after
        5000 -> exit(failure)
    end.

large_send_client(Ref, Bin) ->
    receive
        {ok, {_, LPort}} ->
            Opts = [{active,true},{mode,binary}],
            {ok, Sock} = gen_utp:connect("127.0.0.1", LPort, Opts),
            ok = gen_utp:send(Sock, Bin),
            receive
                {utp, Sock, Reply} ->
                    ?assertMatch(Reply, <<"large send server">>),
                    receive
                        {done, Ref} -> ok
                    after
                        5000 -> exit(failure)
                    end
            after
                5000 -> exit(failure)
            end,
            ok = gen_utp:close(Sock)
    after
        5000 -> exit(failure)
    end,
    ok.

two_servers(Client, Ref) ->
    {ok, LSock} = gen_utp:listen(0, [{active,true}]),
    {ok, Sockname} = gen_utp:sockname(LSock),
    Client ! {Ref, Sockname},
    Self = self(),
    Pid1 = spawn_link(fun() -> two_servers_do_server(Self) end),
    Pid2 = spawn_link(fun() -> two_servers_do_server(Self) end),
    receive
        {utp_async, Sock1, {_, _}} ->
            ok = gen_utp:controlling_process(Sock1, Pid1),
            Pid1 ! {go, Sock1}
    after
        5000 -> exit(failure)
    end,
    receive
        {utp_async, Sock2, {_, _}} ->
            ok = gen_utp:controlling_process(Sock2, Pid2),
            Pid2 ! {go, Sock2}
    after
        5000 -> exit(failure)
    end,
    Client ! {Ref, send},
    receive
        {Pid1, ok} ->
            receive
                {Pid2, ok} ->
                    Pid1 ! check,
                    Pid2 ! check;
                Error ->
                    exit(Error)
            after
                5000 -> exit(failure)
            end;
        Error ->
            exit(Error)
    after
        5000 -> exit(failure)
    end,
    ?assertMatch({message_queue_len,0},
                 erlang:process_info(self(), message_queue_len)),
    ok = gen_utp:close(LSock).

two_servers_do_server(Pid) ->
    Sock = receive
               {go, S} ->
                   S
           after
               5000 -> exit(failure)
           end,
    receive
        {utp, Sock, Msg} ->
            ok = gen_utp:send(Sock, Msg),
            Pid ! {self(), ok};
        Error ->
            exit(Error)
    after
        5000 -> exit(failure)
    end,
    receive
        check ->
            ?assertMatch({message_queue_len,0},
                         erlang:process_info(self(), message_queue_len))
    after
        5000 -> exit(failure)
    end,
    ok = gen_utp:close(Sock).

two_server_client(Ref) ->
    receive
        {Ref, {_, LPort}} ->
            Opts = [{active,true},{mode,binary}],
            {ok, Sock1} = gen_utp:connect("127.0.0.1", LPort, Opts),
            Msg1 = list_to_binary(["two servers", term_to_binary(Ref)]),
            {ok, Sock2} = gen_utp:connect("127.0.0.1", LPort, Opts),
            Msg2 = list_to_binary(lists:reverse(binary_to_list(Msg1))),
            receive
                {Ref, send} ->
                    ok = gen_utp:send(Sock1, Msg1),
                    ok = gen_utp:send(Sock2, Msg2),
                    ok = two_server_client_receive(Sock1, Msg1),
                    ok = two_server_client_receive(Sock2, Msg2)
            after
                5000 -> exit(failure)
            end
    after
        5000 -> exit(failure)
    end,
    ok.

two_server_client_receive(Sock, Msg) ->
    receive
        {utp, Sock, Msg} ->
            ok
    after
        5000 -> exit(failure)
    end,
    ok = gen_utp:close(Sock).

-endif.
