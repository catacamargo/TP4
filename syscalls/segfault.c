#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Manejador de SIGSEGV: solo usa funciones seguras para senales */
static void manejador(int sig)
{
	const char msg[] = "Capture SIGSEGV, salgo de forma controlada\n";
	(void)sig;
	write(STDERR_FILENO, msg, sizeof(msg) - 1);
	_exit(1);
}

int main(int argc, char *argv[])
{
	int *p = NULL;

	if (argc > 1 && strcmp(argv[1], "manejar") == 0) {
		signal(SIGSEGV, manejador);
		printf("Con manejador de SIGSEGV\n");
	} else {
		printf("Sin manejador de SIGSEGV\n");
	}
	fflush(stdout);

	*p = 42;  /* Acceso invalido: direccion 0 */

	printf("Nunca llego aca\n");
	return 0;
}
