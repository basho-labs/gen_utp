%% -------------------------------------------------------------------
%%
%% gen_utp: uTP protocol
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
-module(gen_utp).
-behaviour(gen_server).
-author('Steve Vinoski <vinoski@ieee.org>').

-ifdef(TEST).
-include_lib("eunit/include/eunit.hrl").
-endif.

-export([start_link/0, start/0, stop/0,
         connect/2, connect/3,
         listen/1, close/1, send/2,
         sockname/1, peername/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {
          port :: port()
         }).

-define(UTP_CONNECT_START, 1).
-define(UTP_CONNECT_VALIDATE, 2).
-define(UTP_LISTEN, 3).
-define(UTP_SEND, 4).
-define(UTP_RECV, 5).
-define(UTP_CLOSE, 6).
-define(UTP_SOCKNAME, 7).
-define(UTP_PEERNAME, 8).

-type utpstate() :: #state{}.
-type from() :: {pid(), any()}.
-type utpaddr() :: inet:ip_address() | inet:hostname().
-type utpport() :: inet:port_number().
-type utpsock() :: port().
%%-type utpconnopt() ::
-type utpconnopts() :: [any()].

-spec start_link() -> {ok, pid()} | ignore | {error, term()}.
start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

-spec start() -> {ok, pid()} | ignore | {error, any()}.
start() ->
    gen_server:start({local, ?MODULE}, ?MODULE, [], []).

-spec stop() -> ok.
stop() ->
    gen_server:cast(?MODULE, stop).

-spec connect(utpaddr(), utpport()) -> {ok, utpsock()} | {error, any()}.
connect(Addr, Port) ->
    connect(Addr, Port, []).

-spec connect(utpaddr(), utpport(), utpconnopts()) -> {ok, utpsock()} |
                                                      {error, any()}.
connect(Addr, Port, Opts) when is_tuple(Addr) ->
    try inet_parse:ntoa(Addr) of
        ListAddr ->
            connect(ListAddr, Port, Opts)
    catch
        _:_ ->
            throw(badarg)
    end;
connect(Addr, Port, Opts) ->
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
            Args = {connect, AddrStr, Port, Opts},
            case gen_server:call(?MODULE, Args, infinity) of
                {ok, Sock} ->
                    validate_connect(Sock);
                Fail ->
                    Fail
            end
    end.

-spec listen(utpport()) -> {ok, utpsock()} | {error, any()}.
listen(Port) ->
    gen_server:call(?MODULE, {listen, Port}, infinity).

-spec close(utpsock()) -> ok.
close(Sock) ->
    Ref = make_ref(),
    Args = term_to_binary(term_to_binary(Ref)),
    Result = erlang:port_control(Sock, ?UTP_CLOSE, Args),
    case binary_to_term(Result) of
        wait ->
            receive
                {ok, Ref} -> ok
            end;
        ok ->
            ok
    end,
    true = erlang:port_close(Sock),
    ok.

-spec send(utpsock(), iodata()) -> ok | {error, any()}.
send(Sock, Data) ->
    Result = erlang:port_control(Sock, ?UTP_SEND, Data),
    binary_to_term(Result).

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


-spec init([]) -> ignore |
                  {ok, utpstate()} |
                  {stop, any()}.
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
                         ErrStr = lists:flatten(
                                    io_lib:format("could not load driver ~s: ~p",
                                                  [Shlib, LoadError])),
                         {stop, ErrStr}
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
handle_call({connect, Addr, Port, Opts}, From, #state{port=P}=State) ->
    Caller = term_to_binary(From),
    Args = term_to_binary({Addr, Port, Opts, Caller}),
    try
        erlang:port_control(P, ?UTP_CONNECT_START, Args),
        {noreply, State}
    catch
        _:Reason ->
            {error, Reason}
    end;
handle_call({listen, Port}, From, #state{port=P}=State) ->
    Caller = term_to_binary(From),
    Args = term_to_binary({Port, Caller}),
    try
        erlang:port_control(P, ?UTP_LISTEN, Args),
        {noreply, State}
    catch
        _:Reason ->
            {error, Reason}
    end;
handle_call(_Request, _From, State) ->
    {reply, ok, State}.

-spec handle_cast(any(), utpstate()) -> {noreply, utpstate()} |
                                        {stop, any(), utpstate()}.
handle_cast(stop, State) ->
    {stop, normal, State};
handle_cast(_Msg, State) ->
    {noreply, State}.

-spec handle_info(any(), utpstate()) -> {noreply, utpstate()}.
handle_info({ok, Sock, {Pid,_}=From}, State) ->
    true = erlang:port_connect(Sock, Pid),
    unlink(Sock),
    gen_server:reply(From, {ok, Sock}),
    {noreply, State};
handle_info({error, Reason, From}, State) ->
    gen_server:reply(From, {error, Reason}),
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

validate_connect(Sock) ->
    Ref = make_ref(),
    Args = term_to_binary(term_to_binary(Ref)),
    Bin = erlang:port_control(Sock, ?UTP_CONNECT_VALIDATE, Args),
    case binary_to_term(Bin) of
        ok ->
            {ok, Sock};
        wait ->
            receive
                {ok, Ref} ->
                    {ok, Sock};
                {error, Reason, Ref} ->
                    erlang:port_close(Sock),
                    {error, Reason}
            end;
        Error ->
            erlang:port_close(Sock),
            Error
    end.


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
                       {ok, {Addr, Port}} = gen_utp:sockname(LSock),
                       ok = gen_utp:close(LSock),
                       {error, etimedout} = gen_utp:connect(Addr, Port)
                   end)}
              ]}
     end}.

client_test_() ->
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
               {"uTP simple send test",
                ?_test(
                   begin
                       Self = self(),
                       Ref = make_ref(),
                       spawn_link(fun() ->
                                          ok = simple_send_server(Self, Ref)
                                  end),
                       ok = simple_send_client(Ref)
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
                   end)}
              ]}
     end}.

simple_connect_server(Self, Ref) ->
    {ok, LSock} = gen_utp:listen(0),
    Self ! gen_utp:sockname(LSock),
    receive
        {utp_async, Sock, {Addr, Port}} ->
            true = is_port(Sock),
            true = is_list(Addr),
            true = is_number(Port),
            ok = gen_utp:close(Sock)
    after
        3000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    Self ! {done, Ref},
    ok.

simple_connect_client(Ref) ->
    receive
        {ok, {_, LPort}} ->
            {ok, Sock} = gen_utp:connect("127.0.0.1", LPort),
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

simple_send_server(Self, Ref) ->
    {ok, LSock} = gen_utp:listen(0),
    Self ! gen_utp:sockname(LSock),
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
    Self ! {done, Ref},
    ok.

simple_send_client(Ref) ->
    receive
        {ok, {_, LPort}} ->
            {ok, Sock} = gen_utp:connect("127.0.0.1", LPort),
            ok = gen_utp:send(Sock, <<"simple send client">>),
            receive
                {utp, Sock, <<"simple send server">>} ->
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

two_client_server(Self, Ref) ->
    {ok, LSock} = gen_utp:listen(0),
    Self ! gen_utp:sockname(LSock),
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
    Self ! {done, Ref},
    ok.

two_clients(Ref) ->
    receive
        {ok, {_, LPort}} ->
            {ok, Sock1} = gen_utp:connect("127.0.0.1", LPort),
            ok = gen_utp:send(Sock1, <<"client1">>),
            {ok, Sock2} = gen_utp:connect("127.0.0.1", LPort),
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


-endif.
