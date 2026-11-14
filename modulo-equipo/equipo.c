#include <linux/module.h>   /* Requerido por todos los modulos */
#include <linux/kernel.h>   /* printk / pr_info */
#include <linux/init.h>     /* __init y __exit */
#include <linux/utsname.h>  /* utsname(): hostname y version del kernel */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AprobadoPorChayanne - Sistemas de Computacion UNC");
MODULE_DESCRIPTION("Modulo que imprime el nombre del equipo en el log del kernel");
MODULE_VERSION("1.0");

/* Parametro del modulo: se puede cambiar al cargarlo */
static char *equipo = "AprobadoPorChayanne";
module_param(equipo, charp, 0444);
MODULE_PARM_DESC(equipo, "Nombre del equipo a imprimir");

static int __init equipo_init(void)
{
	pr_info("equipo: modulo cargado\n");
	pr_info("equipo: Equipo = %s\n", equipo);
	pr_info("equipo: Host   = %s\n", utsname()->nodename);
	pr_info("equipo: Kernel = %s\n", utsname()->release);
	return 0;
}

static void __exit equipo_exit(void)
{
	pr_info("equipo: modulo descargado, chau desde %s\n", equipo);
}

module_init(equipo_init);
module_exit(equipo_exit);
