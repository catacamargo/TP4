# TP4 — Módulos del Kernel de Linux

**Sistemas de Computación — FCEFyN, UNC**
**Grupo:** AprobadoPorChayanne

**Integrantes:**
- Catalina Camargo Di Buccio
- Facundo Ariel Muñoz
- Pablo Zbrun

---

## Entorno de trabajo

| | Máquina 1 (Cata) | Máquina 2 (Facu) |
|---|---|---|
| Plataforma | VM VirtualBox | VM VirtualBox |
| Distribución | LMDE 7 "gigi" (base Debian 13 trixie) | Linux Mint 22.3 (base Ubuntu 24.04) |
| Kernel | `6.12.48+deb13-amd64` | `6.14.0-37-generic` |
| Secure Boot | No (arranque BIOS legacy) | — |

Para comprobar Secure Boot se usó `mokutil --sb-state`, que devolvió `EFI variables are not supported on this system`: la VM arranca en modo BIOS legacy, por lo que no hay Secure Boot y se pueden cargar módulos sin firmar.

### Preparación

```bash
sudo apt update
sudo apt-get install -y build-essential checkinstall bison flex \
  libncurses-dev libssl-dev libelf-dev bc linux-headers-$(uname -r) \
  linux-source kmod git strace
git clone https://gitlab.com/sistemas-de-computacion-unc/kenel-modules.git
```

Se verificó que `/lib/modules/$(uname -r)/build` apunte a los headers del kernel en ejecución (`/usr/src/linux-headers-6.12.48+deb13-amd64`). Sin esto no se pueden compilar módulos.

Para evitar la advertencia `Skipping BTF generation ... due to unavailability of vmlinux` se copió el BTF del kernel:

```bash
sudo cp /sys/kernel/btf/vmlinux /lib/modules/$(uname -r)/build/
```

Después de esto, la compilación muestra la etapa `BTF [M]`, lo que indica que se generó la información de tipos para eBPF.

---

## 1. Módulo de ejemplo (`mimodulo`)

El módulo se compila con **kbuild**, el sistema de construcción del kernel, a partir de un Makefile con `obj-m += mimodulo.o` que invoca `make -C /lib/modules/$(uname -r)/build M=$(PWD) modules`.

```bash
make
modinfo mimodulo.ko
sudo insmod mimodulo.ko
sudo dmesg | tail
lsmod | grep mimodulo
grep mimodulo /proc/modules
sudo rmmod mimodulo
```

**Resultados:**
- **Compilación.** Genera `mimodulo.ko` con dos warnings `-Wmissing-prototypes`, porque `modulo_lin_init` y `modulo_lin_clean` no están declaradas `static`. No impiden la compilación.
- **Carga y descarga.** En `dmesg` aparecen `Modulo cargado en el kernel.` y `Modulo descargado del kernel.`
- **`lsmod`.** Muestra `mimodulo  12288  0`: ocupa 12 KiB y ningún otro módulo lo usa.
- **`/proc/modules`.** Muestra `mimodulo 12288 0 - Live 0x0000000000000000 (OE)`:
  - Los flags **(OE)** significan **O** = módulo *out-of-tree* y **E** = módulo sin firmar (*unsigned*).
  - La dirección aparece en cero porque se leyó sin privilegios: `kptr_restrict` oculta las direcciones del kernel a usuarios comunes. Es una medida de seguridad que complementa a KASLR.

### Estado "tainted" del kernel

```bash
cat /proc/sys/kernel/tainted        # 12800
sudo dmesg | grep -iE "taint|call trace"
```

El valor `12800` es una máscara de bits:

| Bit | Valor | Flag | Significado |
|---|---|---|---|
| 9 | 512 | W | El kernel emitió un WARN |
| 12 | 4096 | O | Se cargó un módulo out-of-tree |
| 13 | 8192 | E | Se cargó un módulo sin firmar |
| | **12800** | | |

El kernel ya estaba marcado como *tainted* desde el arranque, por `vboxguest` (Guest Additions de VirtualBox, un módulo out-of-tree y sin firmar), que lo marcó a los 12 segundos de iniciar. Por eso, al cargar nuestro módulo no apareció el mensaje `loading out-of-tree module taints kernel`: se imprime una sola vez por arranque. Los *call traces* que se observan en `dmesg` son warnings propios de la VM y no tienen relación con nuestros módulos.

---

## 2. Módulo propio: `equipo`

Módulo que imprime en el log del kernel el nombre del equipo, el hostname y la versión del kernel. El código está en [`modulo-equipo/`](modulo-equipo/).

Mejoras respecto del ejemplo:
- Funciones `static` con `__init` / `__exit`, lo que elimina los warnings y permite liberar la memoria de inicialización.
- Uso de `pr_info()` en lugar de `printk(KERN_INFO ...)`.
- Un **parámetro de módulo** (`module_param`) para cambiar el nombre al cargarlo.
- Lectura de `utsname()` para obtener el hostname y el release del kernel.
- Metadatos completos: `MODULE_VERSION`, `MODULE_PARM_DESC`.

```bash
make
sudo insmod equipo.ko
lsmod | grep equipo
sudo rmmod equipo
```

Salida en `dmesg -w`:

```
[  140.190271] equipo: modulo cargado
[  140.190275] equipo: Equipo = AprobadoPorChayanne
[  140.190276] equipo: Host   = LinuxCata
[  140.190276] equipo: Kernel = 6.12.48+deb13-amd64
[  159.359449] equipo: modulo descargado, chau desde AprobadoPorChayanne
```

La compilación no generó warnings.

**Incidente durante las pruebas.** En un primer intento, `sudo` quedó bloqueado en estado `D` (*uninterruptible sleep*), esperando una operación dentro del kernel. Esto se diagnosticó con `ps -eo pid,stat,cmd`. El módulo no llegó a cargarse, porque `insmod` nunca se ejecutó. Un proceso en estado `D` no responde a señales, ni siquiera a `SIGKILL`, por lo que fue necesario reiniciar la VM. Después del reinicio, el módulo cargó y descargó sin problemas.

---

## 3. Comparación de `modinfo`: módulo propio vs módulo del sistema

```bash
modinfo equipo.ko
modinfo des_generic
```

| Campo | `equipo.ko` (propio) | `des_generic` (sistema) |
|---|---|---|
| `filename` | `~/kenel-modules/part1/equipo/equipo.ko` | `/lib/modules/6.12.48+deb13-amd64/kernel/crypto/des_generic.ko.xz` (comprimido) |
| `intree` | — | `Y` (forma parte del árbol oficial) |
| `alias` | — | `des`, `des3_ede`, `crypto-des`, etc. |
| `depends` | — | `libdes` |
| `version` / `parm` | `1.0` / `equipo` (charp) | — |
| `vermagic` | `6.12.48+deb13-amd64 SMP preempt mod_unload modversions` | idéntico |
| Firma | **No tiene** | `sig_id: PKCS#7`, `signer: Build time autogenerated kernel key`, `sig_hashalgo: sha256`, `signature` |

**Conclusiones:**
- El módulo del sistema está **firmado** con la clave que Debian generó al compilar el kernel; el nuestro no tiene firma (de ahí el flag **E**).
- El del sistema es **in-tree**; el nuestro es **out-of-tree** (flag **O**).
- Los **alias** permiten que `modprobe` y `udev` carguen el módulo automáticamente cuando algo lo necesita, por ejemplo cuando se pide el algoritmo `des`.
- El **vermagic** coincide porque ambos fueron compilados para el mismo kernel. Si no coincidiera, el kernel rechazaría cargar el módulo.

---

## 4. Módulos cargados en el grupo

```bash
lsmod | awk 'NR>1 {print $1}' | sort > modulos_NOMBRE.txt
diff modulos_cata.txt modulos_Facu.txt
comm -23 modulos_cata.txt modulos_Facu.txt   # solo Cata
comm -13 modulos_cata.txt modulos_Facu.txt   # solo Facu
comm -12 modulos_cata.txt modulos_Facu.txt   # en común
```

Las salidas están en [`lsmod/`](lsmod/).

| | Cantidad |
|---|---|
| Módulos cargados (Cata) | 108 |
| Módulos cargados (Facu) | 75 |
| En común | 57 |
| Solo Cata | 51 |
| Solo Facu | 18 |

### Análisis de las diferencias

Ambas máquinas son VMs de VirtualBox, por lo que el hardware emulado es similar. Las diferencias se explican principalmente por la distribución y la configuración del kernel.

**1. Módulo (`=m`) vs integrado (`=y`).** Debian compila como módulo mucha funcionalidad que Ubuntu/Mint integra directamente en la imagen del kernel. Lo que está integrado funciona igual, pero **no aparece en `lsmod`**. Así se explica la mayor parte de la lista "solo Cata":
- Almacenamiento: `libata`, `ata_piix`, `ata_generic`, `scsi_mod`, `scsi_common`, `sd_mod`, `sr_mod`, `cdrom`, `sg`
- Sistema de archivos: `ext4`, `jbd2`, `mbcache`, `crc16`, `loop`, `squashfs`
- USB y entrada: `usbcore`, `usb_common`, `ehci_hcd`, `ehci_pci`, `ohci_hcd`, `ohci_pci`, `usbhid`, `hid`, `hid_generic`, `evdev`
- ACPI: `ac`, `battery`, `button`
- Video y checksums: `drm`, `drm_kms_helper`, `crc32c_generic`, `crc32c_intel`, `libcrc32c`

**2. Configuración de cada VM.**
- `vboxsf` solo aparece en la máquina de Cata, porque usa una carpeta compartida con el host.
- Facu tiene módulos de audio MIDI (`snd_rawmidi`, `snd_seq_midi`, `snd_seq_midi_event`).
- Para el controlador IDE emulado, Facu usa `pata_acpi` y Cata usa `ata_piix`.

**3. Lo que carga cada distribución por defecto.**
- Ubuntu/Mint carga `dm_mirror`, `dm_log`, `dm_region_hash` y `linear` (device-mapper y RAID), `mac_hid`, `input_leds`, `sch_fq_codel` (su planificador de red por defecto), `vga16fb` y `vgastate` (framebuffer de respaldo).
- Debian carga `ecryptfs`, `rfkill`, `pcspkr` y módulos de energía y telemetría de Intel (`rapl`, `intel_pmc_core`, `pmt_class`, `pmt_telemetry`).

### Módulos disponibles pero no cargados

```bash
find /lib/modules/$(uname -r) -name "*.ko*" | wc -l   # 4228
lsmod | wc -l                                          # 109 (108 + encabezado)
```

Hay **4228 módulos instalados**, pero solo **108 cargados**. El resto queda disponible en disco y se carga bajo demanda: `udev` detecta un dispositivo, lee su identificador y, mediante los alias (`modules.alias`), `modprobe` carga el driver que corresponde.

**¿Qué pasa si el driver de un dispositivo no está disponible?** El kernel detecta el dispositivo en el bus (sigue apareciendo en `lspci` o `lsusb`), pero ningún driver se asocia a él. Entonces:
- no se crea su nodo en `/dev`,
- el sistema no puede comunicarse con el hardware,
- `dmesg` puede mostrar errores o mensajes de dispositivo no reconocido.

Con `lspci -k` se ve qué dispositivos no tienen la línea `Kernel driver in use`.

---

## 5. hw-probe

```bash
sudo apt install -y hw-probe
sudo -E hw-probe -all -upload -check -decode-acpi
```

**Probe:** https://linux-hardware.org/?probe=92e1187449

Se ejecutó en la VM porque ningún integrante del grupo contaba con Linux nativo. Por eso el reporte describe el **hardware virtual de VirtualBox** y no hardware físico. La herramienta lo indica (`NOTE: This is a probe of VM, so its lifetime is limited`). La advertencia `failed to detect EDID` también es esperable, ya que la VM no tiene un monitor físico del cual leer esos datos.

---

## 6. Llamadas al sistema de un hello world (`strace`)

```bash
gcc -o ejemplo_printf ejemplo_printf.c
strace ./ejemplo_printf
strace -c ./ejemplo_printf
```

El programa realizó **35 syscalls**. Secuencia principal:

| Syscall | Función |
|---|---|
| `execve` | Carga el ejecutable |
| `brk`, `mmap` | Reservan memoria (heap y regiones anónimas) |
| `access("/etc/ld.so.preload")` | El loader busca librerías a precargar; falla con `ENOENT` porque el archivo no existe. Es el **único error**, y es normal |
| `openat`, `read`, `pread64`, `fstat`, `mmap`, `close` | El loader dinámico abre `libc.so.6` y la mapea en memoria |
| `mprotect` | Marca regiones como solo lectura (RELRO) |
| `arch_prctl`, `set_tid_address`, `set_robust_list`, `rseq` | Inicialización del hilo y del TLS |
| `prlimit64`, `getrandom` | Límite de stack y semilla aleatoria (stack canaries) |
| `write(1, ...)` ×2 | Los dos `printf` (stdout = FD 1) |
| `exit_group(0)` | Fin del proceso |

**Conclusión.** De las 35 syscalls, solo **2 corresponden al programa en sí** (los `write`). El resto es la preparación que hacen el kernel y el loader dinámico para que el proceso pueda ejecutarse. `printf` es una función de libc que termina invocando la syscall `write`.

---

## 7. Segmentation fault

Programa de prueba: [`syscalls/segfault.c`](syscalls/segfault.c). Escribe en un puntero `NULL` y opcionalmente instala un manejador de `SIGSEGV`.

```bash
gcc -O0 -g -o segfault segfault.c
./segfault;          echo $?
sudo dmesg | tail -3
./segfault manejar;  echo $?
strace ./segfault 2>&1 | tail -5
```

**Resultados:**

| Prueba | Resultado |
|---|---|
| Sin manejador | `Violación de segmento ('core' generado)`, código de salida **139** = 128 + 11 (SIGSEGV) |
| `dmesg` | `segfault at 0 ip ... error 6 in segfault[...]` |
| Con manejador | `Capture SIGSEGV, salgo de forma controlada`, código de salida **1** |
| `strace` | `--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_MAPERR, si_addr=NULL} ---` y `+++ killed by SIGSEGV (core dumped) +++` |

**Interpretación del `error 6`.** Es el código de error de la falla de página en x86 (6 = 0b110):
- bit 2 = 1: el acceso ocurrió en **modo usuario**,
- bit 1 = 1: fue una **escritura**,
- bit 0 = 0: la página **no estaba presente**.

`SEGV_MAPERR` confirma que la dirección no pertenece a ninguna región mapeada del proceso.

**¿Qué es un segmentation fault y cómo lo maneja el kernel?** Es un acceso a memoria que el proceso no tiene permitido: una dirección no mapeada, escribir en una región de solo lectura, ejecutar datos, etc. El flujo es:
1. La MMU genera una excepción de **falla de página**.
2. El kernel la atiende y busca si la dirección pertenece a alguna región válida del proceso.
3. Como no pertenece, le envía la señal **SIGSEGV** al proceso, con información de la falla (`si_addr`, `si_code`), y registra el evento en el log.

**¿Y el programa de usuario?**
- **Sin manejador**, se aplica la acción por defecto de SIGSEGV: el proceso termina y se genera un *core dump*.
- **Con manejador** (`signal`/`sigaction`), el proceso puede atrapar la señal y terminar de forma controlada. No puede "seguir" normalmente, porque la instrucción que falló se volvería a ejecutar.

**En espacio de kernel es distinto.** Si un módulo hace un acceso inválido, no hay un proceso al cual enviarle una señal: se produce un **oops** o un **kernel panic**, que puede dejar todo el sistema inestable.

---

## 8. checkinstall

`checkinstall` intercepta el `make install` de un software compilado desde fuente y genera un **paquete nativo** (`.deb`, `.rpm` o `.tgz`). Así queda registrado en el gestor de paquetes y se puede desinstalar limpiamente, en lugar de dejar archivos sueltos en el sistema.

Se empaquetaron dos programas en un solo paquete (código en [`checkinstall/`](checkinstall/)):
- `hola-chayanne`: hello world.
- `stackframe-chayanne`: recursión que imprime la dirección de las variables locales, el `rbp` y la dirección de retorno de cada stack frame.

```bash
make
sudo checkinstall --pkgname=chayanne-tp4 --pkgversion=1.0 --pkgrelease=1 \
  --maintainer="AprobadoPorChayanne" --pkglicense=GPL --fstrans=no --default
dpkg -l | grep chayanne
dpkg -L chayanne-tp4
```

**Resultado:**
- Se generó `chayanne-tp4_1.0-1_amd64.deb`, que instala `/usr/local/bin/hola-chayanne` y `/usr/local/bin/stackframe-chayanne`.
- `dpkg -l` muestra el paquete con estado `ii` (instalado).
- Se desinstala con `sudo apt remove chayanne-tp4`.

Salida de `stackframe-chayanne`:

```
nivel 0: &local=0x7ffc4d655b7c  rbp=0x7ffc4d655b90  ret=0x55c473a44222
nivel 1: &local=0x7ffc4d655b3c  rbp=0x7ffc4d655b50  ret=0x55c473a441ec  (frame anterior - actual = 64 bytes)
nivel 2: &local=0x7ffc4d655afc  rbp=0x7ffc4d655b10  ret=0x55c473a441ec  (frame anterior - actual = 64 bytes)
nivel 3: &local=0x7ffc4d655abc  rbp=0x7ffc4d655ad0  ret=0x55c473a441ec  (frame anterior - actual = 64 bytes)
```

- Cada frame ocupa **64 bytes**, y cada `rbp` está más abajo que el anterior: la **pila crece hacia direcciones menores**.
- La dirección de retorno es la misma en los niveles 1 a 3 (todos vuelven al mismo punto dentro de `nivel`). En el nivel 0, en cambio, vuelve a `main`.

**Instalación en una segunda máquina.** El `.deb` se instaló en la máquina de Facu (Linux Mint 22.3) con `sudo apt install ./chayanne-tp4_1.0-1_amd64.deb`. Funcionó sin recompilar, porque ambas son x86-64 y el binario solo depende de libc.

---

## 9. Preguntas teóricas

### Funciones disponibles: programa de usuario vs módulo del kernel

| Programa de usuario | Módulo del kernel |
|---|---|
| Biblioteca estándar de C (libc): `printf`, `malloc`, `fopen`, etc. | **No hay libc.** Solo los símbolos que el kernel exporta (`EXPORT_SYMBOL`), visibles en `/proc/kallsyms` |
| Accede al kernel solo mediante **syscalls** (`write`, `read`, `open`…) | Llama directamente a funciones internas: `printk`/`pr_info`, `kmalloc`/`kfree`, `vmalloc`, `copy_to_user`/`copy_from_user`, spinlocks, mutex, timers |
| Tiene `main()`, se ejecuta y termina | Tiene funciones de init/exit (`module_init`/`module_exit`) y se registra para responder a eventos |
| Puede usar punto flotante libremente | El uso de punto flotante está restringido |

### Espacio de usuario, espacio de kernel y espacio de datos

- **Espacio de usuario.** Región de memoria donde corren los procesos comunes, en el anillo 3 de la CPU. Cada proceso tiene su propio espacio de direcciones virtuales, aislado de los demás. Para operaciones privilegiadas debe pedírselas al kernel mediante syscalls.
- **Espacio de kernel.** Región donde reside y se ejecuta el núcleo, incluidos los módulos, en el anillo 0. Tiene acceso a toda la memoria y a todo el hardware. Un error acá afecta a todo el sistema.
- **Espacio de datos.** Los datos que maneja cada lado:
  - del lado del usuario, el espacio de direcciones del proceso (código, datos, heap, stack);
  - del lado del kernel, sus estructuras internas (tablas de procesos, slab, page cache, buffers de drivers).
  
  El paso de datos entre ambos se hace de forma controlada, con `copy_to_user`/`copy_from_user`, para que un proceso no pueda leer ni corromper la memoria del kernel.

### `/dev` y los drivers

`/dev` contiene **nodos de dispositivo**: archivos especiales que representan dispositivos o interfaces. Algunos ejemplos:
- `/dev/sda` y `/dev/nvme0n1`: discos (dispositivos de bloque).
- `/dev/ttyS0`: puerto serie (dispositivo de carácter).
- `/dev/null`, `/dev/zero`, `/dev/random`: dispositivos virtuales.
- `/dev/pts/*`: pseudoterminales.

Cada nodo tiene un número **major**, que identifica al driver, y un número **minor**, que identifica la instancia. Cuando un programa hace `open`/`read`/`write` sobre un nodo, el kernel deriva la operación al driver que corresponde.

Los nodos los crea `udev` cuando un driver se registra. Si no hay driver, el nodo no existe y el dispositivo no se puede usar.

### Diferencia entre un módulo y un programa

| | Programa | Módulo |
|---|---|---|
| Dónde corre | Espacio de usuario (anillo 3) | Espacio de kernel (anillo 0) |
| Ciclo de vida | Empieza en `main()` y termina | `init` lo registra, queda residente respondiendo a eventos, `exit` lo limpia |
| Bibliotecas | libc + syscalls | API interna del kernel |
| Memoria | Su propio espacio virtual, aislado | Comparte el espacio del kernel |
| Un error | Mata solo al proceso (SIGSEGV) | Oops o kernel panic |
| Cómo se ejecuta | `./programa` | `insmod` / `modprobe` |
| Compilación | `gcc` común | kbuild, contra los headers del kernel exacto (`vermagic`) |

### Seguridad del kernel

**Evitar cargar módulos no firmados:**
- `CONFIG_MODULE_SIG` + `CONFIG_MODULE_SIG_FORCE`, o el parámetro de arranque `module.sig_enforce=1`: el kernel rechaza cualquier módulo sin una firma válida.
- **Secure Boot**: en la mayoría de las distribuciones activa automáticamente la verificación obligatoria de firmas.
- **Kernel lockdown** (`lockdown=integrity` o `confidentiality`): impide modificar el kernel en ejecución (`/dev/mem`, kexec sin firmar, etc.).
- `kernel.modules_disabled=1`: después del arranque, bloquea por completo la carga de nuevos módulos.

**Técnicas contra rootkits.** Un rootkit de kernel suele ser un módulo malicioso que se oculta de `lsmod`, intercepta la tabla de syscalls o modifica estructuras internas. Las defensas son:
- Firma obligatoria de módulos y Secure Boot.
- Mecanismos de hardware y del kernel que dificultan la explotación: KASLR, SMEP/SMAP, stack canaries, tabla de syscalls de solo lectura.
- `kptr_restrict` y `dmesg_restrict`, que impiden filtrar direcciones del kernel.
- eBPF con verificador estricto en lugar de módulos para instrumentación.
- Detectores de rootkits e integridad: `chkrootkit`, `rkhunter` y AIDE (que verifica la integridad de los archivos).

**Herramientas:**
- `modinfo`, para ver si un módulo tiene firma.
- `mokutil`, para gestionar las Machine Owner Keys.
- `sign-file`, `pesign` y `sbsign`, para firmar módulos y binarios EFI.

### ¿Qué pasa si un compañero con Secure Boot carga un módulo firmado por nosotros?

**El kernel lo rechaza.** Con Secure Boot activo, el kernel solo acepta módulos firmados con claves en las que confía:
- la clave con la que se compiló el kernel de la distribución,
- las claves del firmware (db),
- las claves **MOK** registradas por el usuario.

Nuestra clave no está en ninguno de esos lugares, así que `insmod` falla con un error del tipo `Key was rejected by service`.

Para que funcione, el compañero tendría que **registrar nuestra clave pública** con `sudo mokutil --import clave.der`, reiniciar y confirmar el registro en la pantalla de MOK Manager.

Además, el módulo debe estar compilado para **su mismo kernel**. Si el `vermagic` no coincide, lo rechaza igual (`Invalid module format`), aunque la firma sea válida.

### Artículo de Ars Technica (2024): parche de Microsoft y GRUB

**Consecuencia principal en sistemas de arranque dual.** En agosto de 2024, una actualización de Windows aplicó una política **SBAT** para bloquear versiones de GRUB vulnerables (CVE-2022-2601). En teoría se iba a aplicar solo en equipos sin arranque dual, pero se aplicó también en muchos que tenían Linux. Como resultado, esos sistemas **dejaron de arrancar Linux** y mostraban el error `Verifying shim SBAT data failed: Security Policy Violation`, porque su shim/GRUB quedó considerado inseguro.

**Implicancia de desactivar Secure Boot como solución.** Permite volver a arrancar, pero **elimina la protección de toda la cadena de arranque**: cualquier bootloader o kernel modificado (por ejemplo, un bootkit) podría ejecutarse sin ser detectado. Lo recomendable es desactivarlo solo temporalmente, borrar la política SBAT (`sudo mokutil --set-sbat-policy delete`), actualizar shim y GRUB, y volver a activarlo.

**Propósito principal de Secure Boot.** Garantizar que durante el arranque solo se ejecute software **firmado por una entidad confiable**, verificando cada eslabón de la cadena: firmware → bootloader (shim/GRUB) → kernel → módulos. Así se evita que malware se cargue antes que el sistema operativo, donde sería muy difícil de detectar.



---


- Presentación de la cátedra
- Artículo de Ars Technica sobre el parche de Microsoft y GRUB (2024)
