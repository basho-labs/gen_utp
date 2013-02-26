%% -------------------------------------------------------------------
%%
%% gen_utp_listen_tests: listen tests for gen_utp
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
-module(gen_utp_listen_tests).
-author('Steve Vinoski <vinoski@ieee.org>').

-include_lib("eunit/include/eunit.hrl").
-include("gen_utp_tests_setup.hrl").

listen_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(_) ->
             {inorder,
              [{"simple listen test",
                fun simple_listen/0},
               {"listen not connected test",
                fun listen_notconn/0},
               {"two listen test",
                fun two_listen/0},
               {"specific interface listen test",
                fun specific_interface/0},
               {"async accept test",
                fun async_accept/0},
               {"accept timeout test",
                fun accept_timeout/0}
              ]}
     end}.

simple_listen() ->
    Ports = length(erlang:ports()),
    {ok, LSock} = gen_utp:listen(0),
    ?assert(erlang:is_port(LSock)),
    ?assertEqual(Ports+1, length(erlang:ports())),
    Self = self(),
    ?assertMatch({connected, Self}, erlang:port_info(LSock, connected)),
    ?assertMatch({error, enotconn}, gen_utp:send(LSock, <<"send">>)),
    {ok, {Addr, Port}} = gen_utp:sockname(LSock),
    ?assert(is_tuple(Addr)),
    ?assert(is_number(Port)),
    ?assertEqual({ok, Port}, gen_utp:port(LSock)),
    ?assertMatch({error, enotconn}, gen_utp:peername(LSock)),
    ?assertMatch(ok, gen_utp:close(LSock)),
    ?assertMatch(undefined, erlang:port_info(LSock)),
    ?assertEqual(Ports, length(erlang:ports())),
    ok.

listen_notconn() ->
    {ok, LSock} = gen_utp:listen(0),
    ?assertMatch({error,enotconn}, gen_utp:send(LSock, "data")),
    ?assertMatch({error,enotconn}, gen_utp:recv(LSock, 0, 1000)),
    ok.

two_listen() ->
    {ok, LSock1} = gen_utp:listen(0),
    {ok, {_, Port1}} = gen_utp:sockname(LSock1),
    ?assertMatch(ok, gen_utp:close(LSock1)),
    {ok, LSock2} = gen_utp:listen(Port1),
    ?assertNotEqual(LSock1, LSock2),
    {ok, {_, Port2}} = gen_utp:sockname(LSock2),
    ?assertEqual(Port1, Port2),
    {ok, LSock3} = gen_utp:listen(0),
    {ok, {_, Port3}} = gen_utp:sockname(LSock3),
    ?assertNotEqual(Port2, Port3),
    ?assertMatch(ok, gen_utp:close(LSock2)),
    ok.

specific_interface() ->
    {ok, LSock1} = gen_utp:listen(0, [{ip, "127.0.0.1"}]),
    ?assertMatch({ok, {{127,0,0,1}, _}}, gen_utp:sockname(LSock1)),
    ?assertMatch(ok, gen_utp:close(LSock1)),
    {ok, LSock2} = gen_utp:listen(0, [{ifaddr, "127.0.0.1"}]),
    ?assertMatch({ok, {{127,0,0,1}, _}}, gen_utp:sockname(LSock2)),
    ?assertMatch(ok, gen_utp:close(LSock2)),
    {ok, LSock3} = gen_utp:listen(0, [{ip, {127,0,0,1}}]),
    ?assertMatch({ok, {{127,0,0,1}, _}}, gen_utp:sockname(LSock3)),
    ?assertMatch(ok, gen_utp:close(LSock3)),
    {ok, LSock4} = gen_utp:listen(0, [{ifaddr, {127,0,0,1}}]),
    ?assertMatch({ok, {{127,0,0,1}, _}}, gen_utp:sockname(LSock4)),
    ?assertMatch(ok, gen_utp:close(LSock4)),
    ok.

async_accept() ->
    {ok, LSock} = gen_utp:listen(0),
    ?assertMatch({ok, _Ref}, gen_utp:async_accept(LSock)),
    ok.

accept_timeout() ->
    {ok, LSock} = gen_utp:listen(0),
    ?assertMatch({error, etimedout}, gen_utp:accept(LSock, 2000)),
    ok.
