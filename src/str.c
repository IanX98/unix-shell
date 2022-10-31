#include <string.h>

#include "str.h"

/*
	Quebra a string em substrings com base no delimitador e acrecenta NULL
	na última posição para ficar de acordo com o formato esperado no execute
	Ex: se delim é " " e src é "ls -l" dest será {"ls", "-l", NULL} e o
	valor 2 será retornado para indicar que existem 2 substrings.

*/
int str_split(char **dest, char *src, char *delim, int dest_size)
{
	int i;
	char *ptr = strtok(src, delim);

	for (i=0; ptr != NULL && i < dest_size - 1; i++) {
		dest[i] = ptr;
		ptr = strtok(NULL, delim);
	}
	dest[i] = NULL;

	return i;
}

/*
	Processo analogamente inverso ao split porém o delimitador se restringe
	ao caractere espaço " "
	Ex: se src é {"ls", "-l", NULL} dest será "ls -l".
*/
void str_join(char *dest, char **src, int len)
{
	int i;

	dest[0] = '\0';
	for (i=0; i < len; i++) {
		strcat(dest, src[i]);
		if (i != len - 1)
			strcat(dest, " ");
	}
}
