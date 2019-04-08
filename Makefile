CC=g++

all: project2

project2:
	$(CC)  fuddu_banana_h.cpp -o project2 

clean:
	rm project2 routing-output*.txt