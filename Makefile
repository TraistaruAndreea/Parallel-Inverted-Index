# build:
# 		gcc main.c -o tema1 -lpthread
# clean:
# 		rm tema1

build:
	g++ -pthread tema1.cpp -o tema1 
clean:
	rm tema1