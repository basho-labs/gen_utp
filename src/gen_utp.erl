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
         connect/3]).
-export([init/1, handle_call/3, handle_cast/2, handle_info/2,
         terminate/2, code_change/3]).

-record(state, {
          port :: port()
         }).

-define(UTP_CONNECT, 1).
-define(UTP_CLOSE, 2).

-type utpstate() :: #state{}.
-type from() :: {pid(), any()}.
-type utpaddr() :: inet:ip_address() | inet:hostname().
-type utpport() :: inet:port_number().
-type utpsock() :: port().

-spec start_link() -> {ok, pid()} | ignore | {error, term()}.
start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, [], []).

-spec start() -> {ok, pid()} | ignore | {error, any()}.
start() ->
    gen_server:start({local, ?MODULE}, ?MODULE, [], []).

-spec stop() -> ok.
stop() ->
    gen_server:cast(?MODULE, stop).

-spec connect(utpaddr(), utpport(), any()) -> {ok, utpsock()} | {error, any()}.
connect(Addr, Port, Opts) when is_tuple(Addr) ->
    try inet_parse:ntoa(Addr) of
        ListAddr ->
            connect(ListAddr, Port, Opts)
    catch
        _:_ ->
            throw(badarg)
    end;
connect(Addr, Port, Opts) ->
    gen_server:call(?MODULE, {connect, Addr, Port, Opts}, infinity).


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
                     _ -> {stop, "could not load driver " ++ Shlib}
                 end,
    case LoadResult of
        ok ->
            Port = erlang:open_port({spawn, Shlib}, [binary]),
            {ok, #state{port=Port}};
        Error ->
            Error
    end.

-spec handle_call(any(), from(), utpstate()) -> {reply, any(), utpstate()}.
handle_call({connect, Addr, Port, Opts}, From, #state{port=P}=State) ->
    Caller = term_to_binary(From),
    Args = term_to_binary({Addr, Port, Opts, Caller}),
    try
        erlang:port_control(P, ?UTP_CONNECT, Args),
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
handle_info({ok, Port, {Pid,_}=From}, State) ->
    erlang:port_connect(Port, Pid),
    unlink(Port),
    gen_server:reply(From, {ok, Port}),
    {noreply, State};
handle_info({error, Reason, From}, State) ->
    gen_server:reply(From, {error, Reason}),
    {noreply, State};
handle_info(_Info, State) ->
    {noreply, State}.

-spec terminate(any(), utpstate()) -> ok.
terminate(_Reason, #state{port=Port}) ->
    erlang:port_close(Port),
    ok.

-spec code_change(any(), utpstate(), any()) -> {ok, utpstate()}.
code_change(_OldVsn, State, _Extra) ->
    {ok, State}.

-ifdef(TEST).

-endif.
