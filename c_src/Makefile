#
# Since rebar doesn't manage dependencies for C++/C code, manage them here.
# The .dep files depend on the source files each .o file depends on. If a
# source file changes, the .cc -> .dep rule fires. The rule deletes the
# corresponding .o file and creates or touches the .dep file. Since "make all"
# here executes in rebar's prebuild script, by the time rebar runs any
# out-of-date .o files will have been deleted and it will rebuild them.
#
TGTS := client.dep coder.dep drv_types.dep globals.dep handler.dep \
	listener.dep main_handler.dep server.dep socket_handler.dep \
	utils.dep utp_handler.dep utpdrv.dep write_queue.dep

all: $(TGTS)

clean:
	@rm -f $(TGTS)

.SUFFIXES: .dep

.cc.dep:
	@rm -f ${@:.dep=.o}
	@touch $@

client.dep: client.cc client.h utp_handler.h socket_handler.h handler.h \
  libutp/utp.h libutp/utypes.h drv_types.h coder.h utils.h \
  write_queue.h globals.h locker.h
coder.dep: coder.cc coder.h
drv_types.dep: drv_types.cc drv_types.h coder.h
globals.dep: globals.cc globals.h
handler.dep: handler.cc handler.h libutp/utp.h libutp/utypes.h globals.h
listener.dep: listener.cc listener.h socket_handler.h handler.h \
  libutp/utp.h libutp/utypes.h drv_types.h coder.h utils.h \
  globals.h main_handler.h utp_handler.h write_queue.h locker.h server.h
main_handler.dep: main_handler.cc main_handler.h handler.h \
  libutp/utp.h libutp/utypes.h \
  utils.h coder.h utp_handler.h socket_handler.h drv_types.h write_queue.h \
  globals.h locker.h client.h listener.h
server.dep: server.cc server.h utp_handler.h socket_handler.h handler.h \
  libutp/utp.h libutp/utypes.h drv_types.h coder.h \
  utils.h write_queue.h listener.h globals.h locker.h
socket_handler.dep: socket_handler.cc socket_handler.h handler.h \
  libutp/utp.h libutp/utypes.h drv_types.h coder.h globals.h utils.h
utils.dep: utils.cc utils.h coder.h globals.h main_handler.h handler.h \
  libutp/utp.h libutp/utypes.h utp_handler.h socket_handler.h \
  drv_types.h write_queue.h
utp_handler.dep: utp_handler.cc utp_handler.h socket_handler.h handler.h \
  libutp/utp.h libutp/utypes.h drv_types.h coder.h \
  utils.h write_queue.h locker.h globals.h main_handler.h
utpdrv.dep: utpdrv.cc globals.h \
  main_handler.h handler.h libutp/utp.h libutp/utypes.h utils.h coder.h \
  utp_handler.h socket_handler.h drv_types.h write_queue.h
write_queue.dep: write_queue.cc write_queue.h
