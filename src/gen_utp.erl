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
         listen/1, listen/2,
         connect/2, connect/3,
         close/1, send/2,
         sockname/1, peername/1]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {
          port :: port()
         }).

%% driver command IDs
-define(UTP_LISTEN, 1).
-define(UTP_CONNECT_START, 2).
-define(UTP_CONNECT_VALIDATE, 3).
-define(UTP_RECV, 4).
-define(UTP_CLOSE, 5).
-define(UTP_SOCKNAME, 6).
-define(UTP_PEERNAME, 7).

%% IDs for listen options
-define(UTP_IP, 1).
-define(UTP_FD, 2).
-define(UTP_PORT, 3).
-define(UTP_LIST, 4).
-define(UTP_BINARY, 5).
-define(UTP_INET, 6).
-define(UTP_INET6, 7).

-type utpstate() :: #state{}.
-type from() :: {pid(), any()}.
-type utpaddr() :: inet:ip_address() | inet:hostname().
-type utpport() :: inet:port_number().
-type utpsock() :: port().
-type utplsnopt() :: {ip,utpaddr()} | {ifaddr,utpaddr()} |
                     {fd,non_neg_integer()} | {port, utpport()} |
                     list | binary | inet | inet6.
-type utplsnopts() :: [utplsnopt()].
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

-spec listen(utpport()) -> {ok, utpsock()} | {error, any()}.
listen(Port) when Port >= 0, Port < 65536 ->
    listen(Port, []).

-spec listen(utpport(), utplsnopts()) -> {ok, utpsock()} | {error, any()}.
listen(Port, Options) when Port >= 0, Port < 65536 ->
    OptBin = options_to_binary(listen, [{port,Port}|Options]),
    Ref = make_ref(),
    Args = term_to_binary({OptBin, term_to_binary(Ref)}),
    try
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

-spec connect(utpaddr(), utpport(), utpconnopts()) -> {ok, utpsock()} |
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
            Ref = make_ref(),
            From = {self(), Ref},
            Args = term_to_binary({AddrStr, Port, Opts, term_to_binary(From)}),
            try
                erlang:port_control(utpdrv, ?UTP_CONNECT_START, Args),
                receive
                    {ok, Sock, From} ->
                        validate_connect(Sock);
                    {error, Fail, From} ->
                        {error, Fail}
                end
            catch
                _:Reason ->
                    {error, Reason}
            end
    end.

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
    try
        true = erlang:port_command(Sock, Data),
        receive
            {utp_reply, Sock, Result} ->
                Result
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
                         LoadErrorStr = erl_ddll:format_error(LoadError),
                         ErrStr = lists:flatten(
                                    io_lib:format("could not load driver ~s: ~p",
                                                  [Shlib, LoadErrorStr])),
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

-record(listen_opts, {
          delivery,
          ip,
          port,
          fd,
          family
         }).
-spec options_to_binary(listen, utplsnopts()) -> binary().
options_to_binary(listen, Opts) ->
    options_to_binary(listen, Opts, #listen_opts{}).
-spec options_to_binary(listen, utplsnopts(), #listen_opts{}) -> binary().
options_to_binary(listen, [], ListenOpts) ->
    OptBin0 = case ListenOpts#listen_opts.delivery of
                  undefined ->
                      <<?UTP_BINARY:8>>;
                  binary ->
                      <<?UTP_BINARY:8>>;
                  list ->
                      <<?UTP_LIST:8>>
              end,
    OptBin1 = case ListenOpts#listen_opts.family of
                  undefined ->
                      <<OptBin0/binary, ?UTP_INET:8>>;
                  inet ->
                      <<OptBin0/binary, ?UTP_INET:8>>;
                  inet6 ->
                      <<OptBin0/binary, ?UTP_INET6:8>>
              end,
    OptBin2 = case ListenOpts#listen_opts.ip of
                  undefined ->
                      OptBin1;
                  AddrStr ->
                      list_to_binary([OptBin1, ?UTP_IP, AddrStr, 0])
              end,
    Port = ListenOpts#listen_opts.port,
    OptBin3 = <<OptBin2/binary, ?UTP_PORT:8, Port:16/big>>,
    case ListenOpts#listen_opts.fd of
        undefined ->
            OptBin3;
        Fd ->
            <<OptBin3/binary, ?UTP_FD:8, Fd:32/big>>
    end;
options_to_binary(listen, [{ip,IpAddr}|Opts], ListenOpts) ->
    NewOpts = ipaddr_to_opts(IpAddr, ListenOpts),
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [{ifaddr,IpAddr}|Opts], ListenOpts) ->
    NewOpts = ipaddr_to_opts(IpAddr, ListenOpts),
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [{fd,Fd}|Opts], ListenOpts) when is_integer(Fd) ->
    NewOpts = ListenOpts#listen_opts{fd=Fd},
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [{port,Port}|Opts], ListenOpts)
  when is_integer(Port), Port >= 0, Port < 65536 ->
    NewOpts = ListenOpts#listen_opts{port=Port},
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [list|Opts], ListenOpts) ->
    NewOpts = ListenOpts#listen_opts{delivery=list},
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [binary|Opts], ListenOpts) ->
    NewOpts = ListenOpts#listen_opts{delivery=binary},
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [inet|Opts], ListenOpts) ->
    NewOpts = ListenOpts#listen_opts{family=inet},
    options_to_binary(listen, Opts, NewOpts);
options_to_binary(listen, [inet6|Opts], ListenOpts) ->
    NewOpts = ListenOpts#listen_opts{family=inet6},
    options_to_binary(listen, Opts, NewOpts).

-spec ipaddr_to_opts(utpaddr(), #listen_opts{}) -> #listen_opts{}.
ipaddr_to_opts(Addr, ListenOpts) when is_tuple(Addr) ->
    try inet_parse:ntoa(Addr) of
        ListAddr ->
            ipaddr_to_opts(ListAddr, ListenOpts)
    catch
        _:_ ->
            throw(badarg)
    end;
ipaddr_to_opts(Addr, ListenOpts) when is_list(Addr) ->
    case inet:getaddr(Addr, inet) of
        {ok, AddrTuple} ->
            ListenOpts#listen_opts{family=inet, ip=inet_parse:ntoa(AddrTuple)};
        _ ->
            case inet:getaddr(Addr, inet6) of
                {ok, AddrTuple} ->
                    ListenOpts#listen_opts{family=inet6,
                                           ip=inet_parse:ntoa(AddrTuple)};
                _Error ->
                    throw(badarg)
            end
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
