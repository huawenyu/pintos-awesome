pintos-awesome
==============

Pintos Awesome Edition  
By Daniel Chen, Ilya Nepomnyashchiy, and Atharv Vaish  

install on fedora 21 with qemu
==============================

[ref](https://pintosiiith.wordpress.com/2012/09/13/install-pintos-with-qemu/)  
Please change the $HOME '/home/wilson' to your own home directory.

source
------
$ cd ~/work  
$ git clone https://github.com/huawenyu/pintos-awesome.git  

qemu
----
Fedora, qemu is called qemu-system-i386  
  
$ sudo yum install qemu  
$ sudo ln -s /bin/qemu-system-i386 /bin/qemu  

set run-path to tools
---------------------
$ PATH=$PATH:~/work/pintos-awesome/src/utils  

patch scripts
-------------
$ patch src/utils/pintos-gdb  
```diff
-GDBMACROS=/usr/class/cs140/pintos/pintos/src/misc/gdb-macros
+GDBMACROS=/home/wilson/work/pintos-awesome/src/misc/gdb-macros
```
  
$ patch src/utils/pintos  
```diff
-    $sim = "bochs" if !defined $sim;
+    $sim = "qemu" if !defined $sim;
  
-	my $name = find_file ('kernel.bin');
+	my $name = find_file ('/home/wilson/work/pintos-awesome/src/threads/build/kernel.b<
```
  
$ patch src/utils/Pintos.pm  
```diff
-    $name = find_file ("loader.bin") if !defined $name;
+    $name = find_file ("/home/wilson/work/pintos-awesome/src/threads/build/loader.bin") if !defined $name;
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
-SIMULATOR = --bochs
+SIMULATOR = --qemu
```
  
$ cd ~/work/pinto-awesome/src/threads  
$ make  

run
---
$ pintos run alarm-multiple  

