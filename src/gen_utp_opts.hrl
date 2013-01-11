%% -------------------------------------------------------------------
%%
%% gen_utp_opts: socket options for uTP protocol
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

%% IDs for binary-encoded options
-define(UTP_IP, 1).
-define(UTP_FD, 2).
-define(UTP_PORT, 3).
-define(UTP_LIST, 4).
-define(UTP_BINARY, 5).
-define(UTP_MODE, 6).
-define(UTP_INET, 7).
-define(UTP_INET6, 8).
-define(UTP_SEND_TMOUT, 9).
-define(UTP_SEND_TMOUT_INFINITE, 10).
-define(UTP_ACTIVE, 11).
-define(UTP_ASYNC_ACCEPT, 12).

%% IDs for values of the active option
-define(UTP_ACTIVE_FALSE, 0).
-define(UTP_ACTIVE_ONCE, 1).
-define(UTP_ACTIVE_TRUE, 2).

-record(utp_options, {
          mode :: list | binary,
          ip :: string(),
          port :: gen_utp:utpport(),
          fd :: non_neg_integer(),
          family :: inet | inet6,
          send_tmout :: pos_integer() | infinity,
          active :: once | boolean(),
          async_accept :: boolean()
         }).
