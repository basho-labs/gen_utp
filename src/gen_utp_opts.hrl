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
-define(UTP_IP_OPT, 1).
-define(UTP_PORT_OPT, 2).
-define(UTP_LIST_OPT, 3).
-define(UTP_BINARY_OPT, 4).
-define(UTP_MODE_OPT, 5).
-define(UTP_INET_OPT, 6).
-define(UTP_INET6_OPT, 7).
-define(UTP_SEND_TMOUT_OPT, 8).
-define(UTP_SEND_TMOUT_INFINITE_OPT, 9).
-define(UTP_ACTIVE_OPT, 10).
-define(UTP_PACKET_OPT, 11).
-define(UTP_HEADER_OPT, 12).
-define(UTP_SNDBUF_OPT, 13).
-define(UTP_RECBUF_OPT, 14).

%% IDs for values of the active option
-define(UTP_ACTIVE_FALSE, 0).
-define(UTP_ACTIVE_ONCE, 1).
-define(UTP_ACTIVE_TRUE, 2).

-record(utp_options, {
          mode :: gen_utp_opts:utpmode(),
          ip :: string(),
          port :: gen_utp:utpport(),
          family :: gen_utp_opts:utpfamily(),
          send_tmout :: gen_utp_opts:utptimeout(),
          active :: gen_utp_opts:utpactive(),
          packet :: gen_utp_opts:utppacketsize(),
          header :: gen_utp_opts:utpheadersize(),
          sndbuf :: gen_utp_opts:utpbufsize(),
          recbuf :: gen_utp_opts:utpbufsize()
         }).
