
LIBS=-lfcgi++ -lfcgi -lpthread -lconfig 
FLAGS=-std=c++17 -Wall -Iframework
FILES=bot.cc

all:	release

release:
	g++ -o bot $(FILES) $(LIBS) $(FLAGS) -O2 -ggdb

debug:
	g++ -o bot $(FILES) $(LIBS) $(FLAGS) -O0 -ggdb


