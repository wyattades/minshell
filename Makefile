CC     = gcc
CFLAGS = -std=c99 -Wall
NAME   = minshell

all: $(NAME)

$(NAME) : lex.yy.c $(NAME).c
	$(CC) $(CFLAGS) -o $(NAME) lex.yy.c $(NAME).c -lfl

lex.yy.c: lex.l
	flex lex.l

clean:
	rm -f $(NAME) lex.yy.c

memcheck:
	valgrind --leak-check=full --show-leak-kinds=all ./$(NAME)
