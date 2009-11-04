all : midichan autoconnect

clean :
	$(RM) midichan autoconnect

% : %.c
	$(CC) --std=c99 -g -Wall -Werror -o $@ $< -lasound
