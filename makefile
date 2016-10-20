MGLDIR=f:\mgllive
OPT=-O6 -mpentium -fomit-frame-pointer -fno-access-control -fdefer-pop -fforce-mem -finline-functions -fno-function-cse -fstrength-reduce -ffast-math -fthread-jumps -fcse-follow-jumps -fcse-skip-blocks -frerun-cse-after-loop -frerun-loop-opt -fgcse -fexpensive-optimizations -fregmove -fschedule-insns -fschedule-insns2 -funroll-loops -fmove-all-movables
#OPT=-O6 -mpentium -fpack-struct -ffast-math -fomit-frame-pointer
#OPT=-g -fpack-struct
COMPILE=gcc -c -Wall -Zdll -Zmt -Zmts -Zomf -D__OS2__ -I$(MGLDIR)\include $(OPT)
RELEASE=r6

all: dualmode.dll dualmode.lib dualmode.a dmlibtest.exe mglserver.exe

distribution: dive.lib dualmode.a dualmode.lib dualmode.dll makefile mglserver.exe dmlibtest.exe dualmode.def mgl.dll ReadMe.OS2
	zip -9 dmlib_$(RELEASE)_srcbin.zip *.c *.h dive.lib dualmode.a dualmode.lib dualmode.dll dualmode.def makefile mglserver.exe dmlibtest.exe mgl.dll ReadMe.OS2

clean:
	rm -f dualmode.a dualmode.lib *.o *.obj *.log dmlibtest.exe dualmode.dll dmlibtest

dualmode.lib: dualmode.dll dualmode.def
	implib dualmode.lib dualmode.def

dualmode.a: dualmode.dll dualmode.def
	emximp -o dualmode.a dualmode.def

dualmode.dll: dualmode.obj controls.obj blitters.obj dualmode.def dmresource.res
	gcc -Zmap -Zstack 32 -Zdll -Zmts -Zomf -o dualmode.dll dualmode.def dualmode.obj controls.obj blitters.obj dmresource.res -L. -ldive

dmlibtest.exe: dmlibtest dualmode.a
	emxbind -bp dmlibtest

dmlibtest: dmlibtest.o
	gcc -Zmt -o dmlibtest dmlibtest.o -L. -ldualmode

dmresource.res: dmlib.rc
	rc -r dmlib.rc dmresource.res

dmlibtest.o: dmlibtest.c dualmode.h controls.h
	gcc -c -Zmap -Zmt -D__OS2__ -I$(MGLDIR)\include dmlibtest.c

controls.obj: controls.c controls.h dualmode.h
	$(COMPILE) controls.c

blitters.obj: blitters.c dualmode.h blitters.h
	$(COMPILE) blitters.c

dualmode.obj: dualmode.c dualmode.h controls.h
	$(COMPILE) dualmode.c

