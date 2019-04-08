CC=g++

all: my-router

my-router:
	$(CC)  fuddu_banana_h.cpp -o my-router 

clean:
	rm my-router routing-output*.txt