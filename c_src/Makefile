#
# Since rebar doesn't manage dependencies for C++/C code, manage them here.
# The .dep files depend on the source files each .o file depends on. If a
# source file changes, the .cc -> .dep rule fires. The rule deletes the
# corresponding .o file and creates or touches the .dep file. Since "make all"
# here executes in rebar's prebuild script, by the time rebar runs any
# out-of-date .o files will have been deleted and it will rebuild them.
#
TGTS := client.dep coder.dep drv_types.dep globals.dep handler.dep \
	listener.dep main_port.dep server.dep utils.dep utp_port.dep utpdrv.dep

all: $(TGTS)

clean:
	@rm -f $(TGTS)

.SUFFIXES: .dep

.cc.dep:
	@rm -f ${@:.dep=.o}
	@touch $@

client.dep: client.cc client.h utp_port.h handler.h libutp/utp.h \
	libutp/utypes.h utils.h coder.h drv_types.h globals.h main_port.h \
	locker.h
coder.dep: coder.cc coder.h
drv_types.dep: drv_types.cc drv_types.h coder.h
globals.dep: globals.cc globals.h main_port.h handler.h drv_types.h \
	libutp/utp.h libutp/utypes.h utils.h coder.h
handler.dep: handler.cc handler.h libutp/utp.h libutp/utypes.h
listener.dep: listener.cc listener.h utp_port.h handler.h server.h \
	libutp/utp.h libutp/utypes.h utils.h coder.h \
	drv_types.h globals.h main_port.h locker.h
main_port.dep: main_port.cc main_port.h handler.h libutp/utp.h \
	libutp/utypes.h utils.h coder.h drv_types.h globals.h locker.h \
	utp_port.h client.h listener.h
server.dep: server.cc server.h utp_port.h handler.h locker.h \
	libutp/utp.h libutp/utypes.h utils.h coder.h \
	drv_types.h listener.h globals.h main_port.h
utils.dep: utils.cc utils.h coder.h libutp/utypes.h drv_types.h utp_port.h \
	globals.h main_port.h handler.h libutp/utp.h
utp_port.dep: utp_port.cc utp_port.h handler.h libutp/utp.h libutp/utypes.h \
	utils.h coder.h drv_types.h locker.h globals.h main_port.h
utpdrv.dep: utpdrv.cc globals.h main_port.h handler.h libutp/utp.h \
	libutp/utypes.h utils.h coder.h drv_types.h
