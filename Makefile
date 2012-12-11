all:
	rebar compile

test: all
	rebar -vv eunit

clean:
	rebar clean
