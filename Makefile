GIMPARGS = $(shell gimptool-2.0 --cflags --libs)
SYSTEM_INSTALL_DIR = $(shell gimptool-2.0 --dry-run --install-admin-bin ./bin/morphop | sed 's/cp \S* \(\S*\)/\1/')
USER_INSTALL_DIR = $(shell gimptool-2.0 --dry-run --install-bin ./bin/morphop | sed 's/cp \S* \(\S*\)/\1/')

make: 
	which gimptool-2.0 && \
	gcc -o ./bin/morphop -Wall -O2 -Wno-unused-variable -Wno-pointer-sign -Wno-parentheses src/*.c $(GIMPARGS) -lm -DGIMP_DISABLE_DEPRECATED
	
install: 
	gimptool-2.0 --install-bin ./bin/morphop
	
uninstall: 
	gimptool-2.0 --uninstall-bin morphop

install-admin:
	gimptool-2.0 --install-admin-bin ./bin/morphop

uninstall-admin:
	gimptool-2.0 --uninstall-admin-bin morphop

clean:
	rm ./bin/morphop

