%% -------------------------------------------------------------------
%%
%% gen_utp_close_tests: socket close tests for gen_utp and the utpdrv driver
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
-module(gen_utp_close_tests).
-author('Steve Vinoski <vinoski@ieee.org>').

-include_lib("eunit/include/eunit.hrl").
-include("gen_utp_tests_setup.hrl").

close_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(_) ->
             [{"close client message test",
               fun close_client/0},
              {"close server message test",
               fun close_server/0}]
     end}.

close_client() ->
    {ok, LSock} = gen_utp:listen(0),
    {ok, {_, Port}} = gen_utp:sockname(LSock),
    {ok, Ref} = gen_utp:async_accept(LSock),
    {ok, Sock} = gen_utp:connect("localhost", Port),
    receive
        {utp_async, LSock, Ref, {ok, ASock}} ->
            ok = gen_utp:close(Sock),
            receive
                {utp_closed, ASock} ->
                    ok
            after
                2000 ->
                    exit(server_not_closed)
            end;
        {utp_async, LSock, Ref, Error} ->
            exit({utp_async, Error})
    after
        2000 ->
            exit(async_accept_fail)
    end,
    ok = gen_utp:close(LSock),
    ok.

close_server() ->
    {ok, LSock} = gen_utp:listen(0),
    {ok, {_, Port}} = gen_utp:sockname(LSock),
    {ok, Ref} = gen_utp:async_accept(LSock),
    {ok, Sock} = gen_utp:connect("localhost", Port),
    receive
        {utp_async, LSock, Ref, {ok, ASock}} ->
            ok = gen_utp:close(ASock),
            receive
                {utp_closed, Sock} ->
                    ok
            after
                2000 ->
                    exit(client_not_closed)
            end;
        {utp_async, LSock, Ref, Error} ->
            exit({utp_async, Error})
    after
        2000 ->
            exit(async_accept_fail)
    end,
    ok = gen_utp:close(LSock),
    ok.
