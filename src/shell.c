#include <stdio.h>

#include "cmd.h"
#include "const.h"

#define IS_INTERATIVE(ARGC) (ARGC == 1)
#define IS_BATCH(ARGC) (ARGC == 2)

int main(int argc, char *argv[])
{
	char str[81];

	int rc_rd;
	int rc_exec;

	FILE *pdata;

	pdata = NULL;

	if (IS_INTERATIVE(argc)) {
		cmd_init(stdin, stdout);
	}
	else if (IS_BATCH(argc)) {
		pdata = fopen(argv[1], "r");

		if (pdata == NULL) {
			printf("Failed to open file '%s'!\n" , argv[1]);
			printf("Exiting...\n");
			return FAILURE;
		}

		cmd_init(pdata, NULL);
	}
	else {
		printf("Error: Invalid number of arguments!\n");
		printf("Exiting...\n");
		return FAILURE;
	}

	do {
		rc_rd = cmd_read_ln(str);
		if (rc_rd > 0)
			rc_exec = cmd_execute(str);
	} while (rc_rd > 0 && rc_exec > 0);

	if (pdata)
		fclose(pdata);

	return 0;
}
