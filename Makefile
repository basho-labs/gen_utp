PLT := gen_utp.plt

all:
	rebar compile

test: all
	rebar -vv eunit

clean:
	rebar clean
	rm -f $(PLT)

dialyzer: $(PLT)
	dialyzer --plt $< -r ebin

$(PLT): all
	dialyzer --build_plt --output_plt $@ -r ebin --apps erts kernel stdlib
