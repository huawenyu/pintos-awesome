pintos
======
[manual](http://web.stanford.edu/class/cs140/projects/pintos/pintos.html)  
  
The Pintos Awesome Edition  
By Daniel Chen, Ilya Nepomnyashchiy, and Atharv Vaish  

install on fedora 21 with bochs|qemu
====================================

[ref](https://pintosiiith.wordpress.com/2012/09/13/install-pintos-with-qemu/)  
Please change the $HOME '/home/wilson' to your own home directory.

source
------
$ cd ~/work  
$ git clone https://github.com/huawenyu/pintos.git  

simulator-qemu
--------------
Fedora, qemu is called qemu-system-i386  
  
$ sudo yum install qemu  
$ sudo ln -s /bin/qemu-system-i386 /bin/qemu  

simulator-bochs
---------------
If install from fedora using 'sudo yum install bochs', there have serval error when start bochs.  
So we can install it from source. If download the latest tar from bochs, even have compile error.  
So we use svn checkout the developing code and import it here.  

```bash
$ svn co http://svn.code.sf.net/p/bochs/code/trunk/bochs bochs
$ cd bochs
$ sudo yum install libX11-devel   <<< Xlib.h
$ sudo yum install libXrandr-devel
$ sudo yum install xorg-x11-server-devel
$ ./configure LDFLAGS='-pthread'  <<< fedora gui/libgui.a(x.o): undefined reference to symbol 'XSetForeground'
$ make
$ sudo make install
```

set path
--------
$ PATH=$PATH:~/work/pintos/src/utils  

patch scripts
-------------
$ patch src/utils/pintos-gdb  
```diff
-GDBMACROS=/usr/class/cs140/pintos/pintos/src/misc/gdb-macros
+GDBMACROS=/home/wilson/work/pintos/src/misc/gdb-macros
```
  
$ patch src/utils/pintos  
```diff
-    $sim = "bochs" if !defined $sim;   <<< if using simulator-bochs
+    $sim = "qemu" if !defined $sim;    <<< if using simulator-qemu
  
-	my $name = find_file ('kernel.bin');
+	my $name = find_file ('/home/wilson/proj/pintos/src/threads/build/kernel.b<
```
  
$ patch src/utils/Pintos.pm  
```diff
-    $name = find_file ("loader.bin") if !defined $name;
+    $name = find_file ("/home/wilson/work/pintos/src/threads/build/loader.bin") if !defined $name;
```

patch tools code if compile fail
--------------------------------
$ cd src/utils  
$ make  
squish-pty.c:10:21: fatal error: stropts.h: No such file or directory  
 #include <stropts.h>  
  
$ patch src/utils/squish-pty.c  
```diff
+/*
 #include <stropts.h>
+*/

   /* System V implementations need STREAMS configuration for the
      slave. */
+  /*
   if (isastream (slave))
     {
       if (ioctl (slave, I_PUSH, "ptem") < 0
           || ioctl (slave, I_PUSH, "ldterm") < 0)
         fail_io ("ioctl");
     }
+  */
```
  
$ patch src/utils/squish-unix.c  
```diff
+/*
 #include <stropts.h>
+*/
```
  
make tools
----------
$ cd src/utils  
$ make  

make pintos kernel
------------------
$ patch src/threads/Make.vars  
```diff
-SIMULATOR = --bochs   <<< if using simulator-bochs
+SIMULATOR = --qemu    <<< if using simulator-qemu
```
  
$ cd ~/work/pinto/src/threads  
$ make  

check setup
-----------
$ pintos run alarm-multiple  

TESTS
-----
```bash
$ cd src/threads/build
$ make check
$ make check VERBOSE=1    <<< output more detail info
```
