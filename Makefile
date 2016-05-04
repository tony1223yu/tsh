all:
	gcc tsh.c tsh_cmd.c -g -o tsh

clean:
	rm tsh

install:
	cp tsh /usr/local/bin/tsh
