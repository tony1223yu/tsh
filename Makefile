all:
	gcc tsh.c tsh_cmd.c -o tsh

clean:
	rm tsh
