
all:np_simple.cpp np_single_proc.cpp np_multi_proc.cpp
	g++ np_simple.cpp -o np_simple
	g++ np_single_proc.cpp -o np_single_proc
	g++ np_multi_proc.cpp -pthread -o np_multi_proc

clean:
	rm np_simple
	rm np_single_proc
	rm np_multi_proc
