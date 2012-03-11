all:
	g++ -lcurl -shared -fPIC -Wall -g mysql_address_normalize.cc -o mysql_address_normalize.so
