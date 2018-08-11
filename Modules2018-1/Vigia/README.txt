Este ejemplo es una adaptacion del tutorial incluido
(archivo "device drivers tutorial.pdf") y bajado de:
http://www.freesoftwaremagazine.com/articles/drivers_linux

---

Guia rapida:

Lo siguiente se debe realizar parados en
el directorio en donde se encuentra este README.txt

+ Compilacion (puede ser en modo usuario):
$ make
...
$ ls
... vigia.ko ...

+ Instalacion (en modo root)

# mknod /dev/vigia c 61 0
# chmod a+rw /dev/vigia
# insmod vigia.ko
# dmesg | tail
...
[...........] Inserting vigia module
#

+ Testing (en modo usuario preferentemente)

Ud. necesitara crear multiples shells independientes.  Luego
siga las instrucciones del enunciado de la tarea 3.

+ Desinstalar el modulo

# rmmod vigia.ko
#
