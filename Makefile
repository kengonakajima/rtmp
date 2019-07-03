OPENSSLLIBS=/usr/local/Cellar/openssl/1.0.2r/lib/libssl.a /usr/local/Cellar/openssl/1.0.2r/lib/libcrypto.a 
player : main.o
	g++ -o player main.o librtmp/librtmp.a $(OPENSSLLIBS) -lz



main.o : main.cpp
	g++ -c main.cpp 

