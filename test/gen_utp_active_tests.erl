%% -------------------------------------------------------------------
%%
%% gen_utp_active_tests: active mode tests for gen_utp
%%
%% Copyright (c) 2013 Basho Technologies, Inc. All Rights Reserved.
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
-module(gen_utp_active_tests).
-author('Steve Vinoski <vinoski@ieee.org>').

-include_lib("eunit/include/eunit.hrl").
-include("gen_utp_tests_setup.hrl").

active_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(_) ->
             {inorder,
              [{"active false test",
                fun active_false/0},
               {"active once test",
                fun active_once/0},
               {"active true test",
                fun active_true/0}
              ]}
     end}.

active_false() ->
    {ok, LSock} = gen_utp:listen(0, [{mode,binary},{active,false}]),
    ok = gen_utp:async_accept(LSock),
    {ok, {_, Port}} = gen_utp:sockname(LSock),
    {ok, Sock} = gen_utp:connect("localhost", Port, [binary]),
    receive
        {utp_async, ASock, _} ->
            Words = ["We", "make", "Riak,", "the", "most", "powerful",
                     "open-source,", "distributed", "database", "you'll",
                     "ever", "put", "into", "production."],
            ?assertEqual([ok || _ <- Words],
                         [gen_utp:send(Sock, Data) || Data <- Words]),
            AllBin = list_to_binary(Words),
            timer:sleep(500),
            ?assertMatch({ok, AllBin}, gen_utp:recv(ASock, 0, 2000)),
            ok = gen_utp:close(ASock),
            ok = gen_utp:close(Sock)
    after
        3000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    ok.

active_once() ->
    {ok, LSock} = gen_utp:listen(0, [{mode,binary},{active,false},{packet,1}]),
    ok = gen_utp:async_accept(LSock),
    {ok, {_, Port}} = gen_utp:sockname(LSock),
    {ok, Sock} = gen_utp:connect("localhost", Port, [binary,{packet,1}]),
    receive
        {utp_async, ASock, _} ->
            Words = ["We", "make", "Riak,", "the", "most", "powerful",
                     "open-source,", "distributed", "database", "you'll",
                     "ever", "put", "into", "production."],
            ?assertEqual([ok || _ <- Words],
                         [gen_utp:send(Sock, Data) || Data <- Words]),
            timer:sleep(500),
            F = fun(Fn, Acc) ->
                        ok = gen_utp:setopts(ASock, [{active,once}]),
                        receive
                            {utp, ASock, Word} ->
                                Fn(Fn, [binary_to_list(Word)|Acc])
                        after
                            500 ->
                                lists:reverse(Acc)
                        end
                end,
            ?assertMatch(Words, F(F, [])),
            ok = gen_utp:close(ASock),
            ok = gen_utp:close(Sock)
    after
        3000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    ok.

active_true() ->
    {ok, LSock} = gen_utp:listen(0, [{mode,binary},{active,false}]),
    ok = gen_utp:async_accept(LSock),
    {ok, {_, Port}} = gen_utp:sockname(LSock),
    {ok, Sock} = gen_utp:connect("localhost", Port, [binary]),
    receive
        {utp_async, ASock, _} ->
            ok = gen_utp:setopts(ASock, [{active,true}]),
            Words = ["We", "make", "Riak,", "the", "most", "powerful",
                     "open-source,", "distributed", "database", "you'll",
                     "ever", "put", "into", "production."],
            ?assertEqual([ok || _ <- Words],
                         [gen_utp:send(Sock, Data) || Data <- Words]),
            AllBin = list_to_binary(Words),
            timer:sleep(500),
            F = fun(Fn, Bin) ->
                        receive
                            {utp, ASock, Data} ->
                                Fn(Fn, <<Bin/binary, Data/binary>>)
                        after
                            500 ->
                                Bin
                        end
                end,
            ?assertMatch(AllBin, F(F, <<>>)),
            ok = gen_utp:close(ASock),
            ok = gen_utp:close(Sock)
    after
        3000 -> exit(failure)
    end,
    ok = gen_utp:close(LSock),
    ok.
