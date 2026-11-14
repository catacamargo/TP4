#include <stdio.h>
#include <stddef.h>

/* Cada llamada crea un nuevo stack frame; imprimimos sus direcciones */
static void nivel(int n, void *rbp_anterior)
{
	int local = n;
	void *rbp = __builtin_frame_address(0);   /* base del frame actual (rbp) */
	void *ret = __builtin_return_address(0);  /* direccion de retorno */

	printf("nivel %d: &local=%p  rbp=%p  ret=%p", n, (void *)&local, rbp, ret);
	if (rbp_anterior != NULL) {
		printf("  (frame anterior - actual = %ld bytes)",
		       (long)((char *)rbp_anterior - (char *)rbp));
	}
	printf("\n");

	if (n < 3) {
		nivel(n + 1, rbp);
	} else {
		printf("Fondo de la recursion: la pila crece hacia direcciones menores\n");
	}
}

int main(void)
{
	printf("Demo de stack frames (x86-64)\n");
	nivel(0, NULL);
	return 0;
}
