%% -------------------------------------------------------------------
%%
%% gen_utp_port_tests: port number tests for gen_utp
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
-module(gen_utp_port_tests).
-author('Steve Vinoski <vinoski@ieee.org>').

-include_lib("eunit/include/eunit.hrl").
-include("gen_utp_tests_setup.hrl").

port_number_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(_) ->
             {"port number range test",
              fun port_number_range/0}
     end}.

port_number_range() ->
    Error = {error, function_clause},
    ?assertMatch(Error,
                 try gen_utp:listen(-1) catch C1:R1 -> {C1,R1} end),
    ?assertMatch(Error,
                 try gen_utp:listen(65536) catch C2:R2 -> {C2,R2} end),
    ?assertMatch(Error,
                 try gen_utp:connect("localhost", -1)
                 catch C3:R3 -> {C3,R3} end),
    ?assertMatch(Error,
                 try gen_utp:connect("localhost", 0) catch C4:R4 -> {C4,R4} end),
    ?assertMatch(Error,
                 try gen_utp:connect("localhost", 65536)
                 catch C5:R5 -> {C5,R5} end),
    ok.
