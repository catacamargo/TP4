# Módulo de kernel Linux: compilación cruzada para ARM

Práctica de módulos de kernel de Linux. Se crosscompila un módulo `hello world` en una PC x86_64 para ARM (aarch64), se verifica que **no se puede cargar en la PC** y que **sí se puede cargar en una Raspberry Pi** (emulada con QEMU).

## Contenido del repositorio

| Archivo | Descripción |
|---|---|
| `hello-1.c` | Módulo de kernel mínimo: imprime un mensaje al cargarse y otro al descargarse |
| `Makefile` | Makefile de kbuild preparado para crosscompilar (`ARCH=arm64`, `CROSS_COMPILE=aarch64-linux-gnu-`) |

## Entorno

| Elemento | Detalle |
|---|---|
| PC (host) | Linux x86_64 |
| Toolchain | `gcc-aarch64-linux-gnu` |
| Raspberry Pi (target) | Emulada con QEMU (`qemu-system-aarch64 -machine raspi3b`) |
| Imagen | Raspberry Pi OS (bullseye) arm64, release 2023-05-03 |
| Kernel de la Pi | `6.1.21-v8+` (aarch64) |
| Fuentes del kernel | Repositorio `raspberrypi/linux`, tag `1.20230405` (kernel 6.1.21) |

## Código

### `hello-1.c`

```c
#include <linux/module.h>
#include <linux/kernel.h>

static int __init hello_init(void)
{
    pr_info("Hello, world 1\n");
    return 0;
}

static void __exit hello_exit(void)
{
    pr_info("Goodbye, world 1\n");
}

module_init(hello_init);
module_exit(hello_exit);
MODULE_LICENSE("GPL");
```

### `Makefile`

```make
obj-m += hello-1.o
KDIR ?= $(HOME)/linux

all:
	make -C $(KDIR) M=$(PWD) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
clean:
	make -C $(KDIR) M=$(PWD) clean
```

A diferencia del Makefile de un módulo nativo (que usa `/lib/modules/$(uname -r)/build`), acá `KDIR` apunta al código fuente del kernel de la Raspberry y se definen `ARCH` y `CROSS_COMPILE`.

## Procedimiento

### 1. Levantar la Raspberry emulada

Se usa la imagen Docker de Memfault, que arranca la Raspberry Pi OS en QEMU y expone SSH en el puerto 2222:

```bash
sudo docker run -d --rm --name rpi --cpus=2 -p 2222:2222 \
  stawiski/qemu-raspberrypi-3b:2023-05-03-raspios-bullseye-arm64
```

Usuario: `pi`, contraseña: `raspberry`. Cuando termina de bootear:

```bash
ssh -p 2222 pi@localhost
uname -r && uname -m          # 6.1.21-v8+ / aarch64
sudo modprobe configs
zcat /proc/config.gz > ~/config-pi
exit
scp -P 2222 pi@localhost:config-pi ~/
```

### 2. Instalar el toolchain en la PC

```bash
sudo apt install build-essential bc bison flex libssl-dev libelf-dev git gcc-aarch64-linux-gnu
```

### 3. Preparar el kernel de la Pi

Hay que usar la misma versión que corre la Pi emulada (6.1.21). La rama `rpi-6.1.y` trae el último 6.1.x, así que se clona el tag del firmware de abril de 2023:

```bash
git clone --depth=1 --branch 1.20230405 https://github.com/raspberrypi/linux ~/linux
cd ~/linux
cp ~/config-pi .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_prepare
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- kernelrelease    # 6.1.21-v8+
```

### 4. Compilar el módulo

```bash
cd ~/lkm
make
file hello-1.ko
modinfo hello-1.ko | grep vermagic
```

Salida:

```
hello-1.ko: ELF 64-bit LSB relocatable, ARM aarch64, version 1 (SYSV), ...
vermagic:       6.1.21-v8+ SMP preempt mod_unload modversions aarch64
```

El `.ko` es ARM aarch64 y su `vermagic` coincide con el kernel de la Pi emulada.

### 5. Intentar cargarlo en la PC (debe fallar)

```bash
sudo insmod ./hello-1.ko
```

```
insmod: ERROR: could not insert module ./hello-1.ko: Invalid module format
```

Falla porque el módulo está compilado para ARM y la PC es x86_64.

### 6. Copiarlo a la Raspberry y cargarlo

```bash
scp -P 2222 hello-1.ko pi@localhost:~
ssh -p 2222 pi@localhost
```

Dentro de la Pi:

```bash
sudo insmod hello-1.ko
lsmod | grep hello
dmesg | tail
sudo rmmod hello_1
dmesg | tail
```

Salida:

```
hello_1                16384  0
[ 1469.505661] hello_1: no symbol version for module_layout
[ 1469.507239] hello_1: loading out-of-tree module taints kernel.
[ 1469.519980] Hello, world 1
[ 1491.839147] Goodbye, world 1
```

## Resultados

| Prueba | Resultado |
|---|---|
| Compilación cruzada a ARM aarch64 | Correcta (`file` confirma `ARM aarch64`) |
| Carga en la PC (x86_64) | Falla: `Invalid module format` |
| Carga en la Raspberry (aarch64) | Correcta: aparece en `lsmod` y `dmesg` muestra `Hello, world 1` |
| Descarga en la Raspberry | Correcta: `dmesg` muestra `Goodbye, world 1` |

## Notas

- **Warnings de `Module.symvers`:** durante el `make` aparecen avisos porque el kernel solo se preparó con `modules_prepare` y no se compiló completo, así que falta `Module.symvers`. Por eso al cargar el módulo el kernel informa `no symbol version for module_layout`: el módulo carga igual, pero sin verificación de CRC de símbolos. Para evitarlo hay que compilar el kernel completo (`make ... modules`) y recompilar el módulo.
- **`taints kernel`:** es normal al cargar cualquier módulo externo al árbol del kernel.
- **Limpieza:** `make clean` borra los archivos generados; `sudo docker stop rpi` detiene la Raspberry emulada.

## Referencias

- [The Linux Kernel Module Programming Guide (2.6), sección 2.2: Compiling Kernel Modules](https://tldp.org/LDP/lkmpg/2.6/html/x181.html)
- [Emulating a Raspberry Pi in QEMU (Memfault Interrupt)](https://interrupt.memfault.com/blog/emulating-raspberry-pi-in-qemu)
