# Copyright 2012 William Hart. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice, 
#      this list of conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright notice,
#      this list of conditions and the following disclaimer in the documentation
#      and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY William Hart ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO 
# EVENT SHALL William Hart OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

INC=-I/usr/local/include -I./gc/include
LIB=-L/usr/local/lib -L./gc/lib
OBJS=inference.o environment.o types.o serial.o symbol.o exception.o ast.o parser.o
# backend.o
HEADERS=ast.h exception.h symbol.h serial.h types.h environment.h inference.h
# backend.h
CS_FLAGS=-O2 -g -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS

bacon: bacon.c $(HEADERS) $(OBJS)
	g++ $(CS_FLAGS) bacon.c -o $(INC) $(OBJS) $(LIB) -lgc `/usr/local/bin/llvm-config --libs --cflags --ldflags core analysis executionengine jit interpreter native` -o bacon -ldl -lpthread

ast.o: ast.c $(HEADERS)
	gcc $(CS_FLAGS) -c ast.c -o ast.o $(INC)

exception.o: exception.c $(HEADERS)
	gcc $(CS_FLAGS) -c exception.c -o exception.o $(INC)

parser.o: parser.c $(HEADERS)
	gcc $(CS_FLAGS) -c parser.c -o parser.o $(INC)

symbol.o: symbol.c $(HEADERS)
	gcc $(CS_FLAGS) -c symbol.c -o symbol.o $(INC)

types.o: types.c $(HEADERS)
	gcc $(CS_FLAGS) -c types.c -o types.o $(INC)

environment.o: environment.c $(HEADERS)
	gcc $(CS_FLAGS) -c environment.c -o environment.o $(INC)

inference.o: inference.c $(HEADERS)
	gcc $(CS_FLAGS) -c inference.c -o inference.o $(INC)

serial.o: serial.c $(HEADERS)
	gcc $(CS_FLAGS) -c serial.c -o serial.o $(INC)

backend.o: backend.c $(HEADERS)
	gcc $(CS_FLAGS) -c backend.c -o backend.o $(INC)

parser.c: greg parser.leg
	greg-0.4.3/greg -o parser.c parser.leg

doc:
	pdflatex --output-format=pdf -output-directory doc doc/bacon.tex

greg:
	$(MAKE) -C greg-0.4.3

clean:
	rm -f *.o
	rm -f greg-0.4.3/*.o
	rm -f bacon 
	rm -f greg-0.4.3/greg
	rm -f parser.c

.PHONY: doc clean 

