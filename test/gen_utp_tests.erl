%% -------------------------------------------------------------------
%%
%% gen_utp_tests: tests for gen_utp and the utpdrv driver
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
-module(gen_utp_tests).
-author('Steve Vinoski <vinoski@ieee.org>').

-include_lib("eunit/include/eunit.hrl").

setup() ->
    gen_utp:start_link().

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
     fun(_) ->
             {"uTP port number range test",
              ?_test(
                 begin
                     Error = {error, function_clause},
                     ?assertMatch(Error,
                                  try gen_utp:listen(-1)
                                  catch C1:R1 -> {C1,R1} end),
                     ?assertMatch(Error,
                                  try gen_utp:listen(65536)
                                  catch C2:R2 -> {C2,R2} end),
                     ?assertMatch(Error,
                                  try gen_utp:connect("localhost", -1)
                                  catch C3:R3 -> {C3,R3} end),
                     ?assertMatch(Error,
                                  try gen_utp:connect("localhost", 0)
                                  catch C4:R4 -> {C4,R4} end),
                     ?assertMatch(Error,
                                  try gen_utp:connect("localhost", 65536)
                                  catch C5:R5 -> {C5,R5} end)
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
                       ?assert(erlang:is_port(LSock)),
                       ?assertEqual(Ports+1, length(erlang:ports())),
                       Self = self(),
                       ?assertMatch({connected, Self},
                                    erlang:port_info(LSock, connected)),
                       ?assertMatch({error, enotconn},
                                    gen_utp:send(LSock, <<"send">>)),
                       {ok, {Addr, Port}} = gen_utp:sockname(LSock),
                       ?assert(is_tuple(Addr)),
                       ?assert(is_number(Port)),
                       ?assertMatch({error, enotconn}, gen_utp:peername(LSock)),
                       ?assertMatch(ok, gen_utp:close(LSock)),
                       ?assertMatch(undefined, erlang:port_info(LSock)),
                       ?assertEqual(Ports, length(erlang:ports())),
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
                       ?assertMatch({ok, {{127,0,0,1}, _}},
                                    gen_utp:sockname(LSock1)),
                       ok = gen_utp:close(LSock1),
                       {ok, LSock2} = gen_utp:listen(0, [{ifaddr, "127.0.0.1"}]),
                       ?assertMatch({ok, {{127,0,0,1}, _}},
                                    gen_utp:sockname(LSock2)),
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
                       ?assertMatch({error, etimedout},
                                    gen_utp:connect("localhost", Port))
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
