#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "const.h"
#include "str.h"

#include "cmd.h"

#define CMD_LIMIT 41
#define CMD_SIZE 81

enum STYLES {
	STYLE_SEQ,
	STYLE_PAR
};

struct cmd_shell {
	enum STYLES style;
	FILE *pin;
	FILE *pout;
	char last_cmd[CMD_SIZE];
};

struct cmd_shell shell; // Armazena o "estado" do shell

static FILE *redirect(char *filepath, char *mode)
{
	int fd;
	FILE *pdata;
	char *tmp_fp[2];

	// Exclui os espaços do nome do arquivo
	str_split(tmp_fp, filepath, " ", 2);

	// Abre o arquivo no modo especificado
	pdata = fopen(tmp_fp[0], mode);
	if (pdata == NULL) {
		printf("Failed to redirect command\n");
		return NULL;
	}

	// pega o file descriptor que vai ser usado com dup2
	fd = fileno(pdata);
	if (fd < 0) {
		fclose(pdata);
		printf("Failed to redirect command\n");
		return NULL;
	}

	// se for no modo leitura redireciona a saída padrão para o arquivo
	if (mode[0] == 'r')
		dup2(fd, STDIN_FILENO);
	else // se não redireciona a entrada padrão para o arquivo
		dup2(fd, STDOUT_FILENO);

	return pdata;
}

static int execute(char **cmd)
{
	int rc;

	/*
	   Executa os comandos no utilizando o pid do processo atual e encerra
	   o processo.

	   Comando precisa estar formatado para ser executado
	   Ex: Para executar o comando "ls -l", a chamada da função deve ser
	       execvp("ls", {"ls", "-l", NULL});
	*/
	rc = execvp(cmd[0], cmd);
	if (rc) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return FAILURE;
	}

	return SUCCESS;
}

/*
	Avalia qual o comando passado e dependendo da expressão e executa

	exec é um indicador para identificar se é necessário executar ou não.

	OBS: A função é chamada tanto pelo processo pai, quanto pelo filho.
	O processo pai guarda o estado atual (paralelo, sequencial, sair etc)
	e o processo filho que executa de fato os comandos.
*/
static int run(char *cmd, int exec)
{
	int rc;
	int n_splits;
	char *splitted_cmd[CMD_LIMIT];
	char lst_cmd[CMD_SIZE];

	rc = SUCCESS;
	/*
		Quebra a string em substrings no formato esperado no execute
		Ex: "ls -l" -> {"ls", "-l", NULL}

	*/
	n_splits = str_split(splitted_cmd, cmd, " ", CMD_LIMIT);

	if (n_splits == 1 && strcmp(splitted_cmd[0], "!!") == 0) { // history

		/* checa se existe algum comando no historico e printa somente
		se estiver sendo executado no processo pai */
		if (!exec && shell.last_cmd[0] == '\0')
			fprintf(shell.pout, "No commands\n");
		else if(!exec && shell.last_cmd[0] != '\0')
			fprintf(shell.pout, "%s\n", shell.last_cmd);

		// copia o último comando para uma variável auxiliar
		strcpy(lst_cmd, shell.last_cmd);
		/* quebra o ultimo comando e armazena em splitted_cmd
		para ser executado */
		n_splits = str_split(splitted_cmd, lst_cmd, " ", CMD_LIMIT);
	}

	if (n_splits == 1 && strcmp(splitted_cmd[0], "exit") == 0) {
		rc = FAILURE;
	}
	else if (n_splits == 2 && strcmp(splitted_cmd[0], "style") == 0 &&
	         strcmp(splitted_cmd[1], "parallel") == 0) {
		shell.style = STYLE_PAR;
	}
	else if (n_splits == 2 && strcmp(splitted_cmd[0], "style") == 0 &&
	         strcmp(splitted_cmd[1], "sequential") == 0) {
		shell.style = STYLE_SEQ;
	}
	else if (exec) {
		rc = execute(splitted_cmd);
	}

	/* Concatena o último comando executado em uma única string e armazena
	em shell.last_cmd. Ex: {"ls", "-l", NULL} -> "ls -l" */
	str_join(shell.last_cmd, splitted_cmd, n_splits);

	return rc;
}


/*
	Verifica se um comando tem pipes "|" e executa de acordo.
*/
static int check_pipe_and_run(char *cmd, int exec)
{
	int i;
	int rc;
	int pid;
	int pid_stat;
	int n_splits;
	int pipe_prev;
	int pipe_fd[2];
	char *pipes[CMD_LIMIT];

	/*
		Quebra o comando em pipes
		Ex1: "ls -l" -> {"ls -l", NULL}
		Ex2: "ls -l | grep a" -> {"ls -l ", " grep a", NULL}

		OBS: No Ex1 n_splits é 1 e no Ex2 n_splits é 2.
	*/
	n_splits = str_split(pipes, cmd, "|", CMD_LIMIT);

	if (exec) {
		for(i=0; i < n_splits; i++) {
			/*
				Pipe é unidirecional e usado para comunicação
				entre processos, nesse caso é inicializado com
				a função pipe().
				pipe_fd será utilizado para a leitura/escrita
				Escrita -> pipe[1] -> pipe[0] -> Leitura
			*/
			if (pipe(pipe_fd) == FAILURE) {
				fprintf(stderr, "Error: %s\n", strerror(errno));
				return FAILURE;
			}

			/*
				cria um processo pai e um filho e ambos os
				processos executam o código abaixo.
			*/
			pid = fork();
			if (pid == FAILURE) {
				close(pipe_fd[READ_IDX]);
				close(pipe_fd[WRITE_IDX]);
				fprintf(stderr, "Error: %s\n", strerror(errno));
				return FAILURE;
			}
			else if (pid == CHILD) { // processo filho
				/* Se não for a primeira vez, redireciona o
				   STDIN para pipe_prev, ou seja, o conteúdo de
				   pipe_prev vai ser usado como entrada para o
				   próximo comando */
				if (i != 0) {
					dup2(pipe_prev, STDIN_FILENO);
					close(pipe_prev);
				}

				/* Se não for a ultima vez, redireciona o STDOUT
				   para o pipe de escrita, ou seja vai escrever
				   tudo que iria para o stdout no pipe */
				if (i != n_splits - 1) {
					dup2(pipe_fd[WRITE_IDX], STDOUT_FILENO);
					close(pipe_fd[WRITE_IDX]);
				}

				rc = run(pipes[i], exec);
				if (rc < 0)
					return FAILURE;
			}
			else { // processo pai
				close(pipe_prev);
				close(pipe_fd[WRITE_IDX]);

				/* Se não for a ultima vez, copia o file
				   descriptor do pipe leitura para pipe_prev,
				   permitindo ler o que foi escrito no processo
				   filho utilizando pipe_prev, pois como é
				   criado um pipe novo a cada iteração a
				   informação do comando anterior seria perdida,
				   pois não leria possível ler utilizando o
				   pipe[0] */
				pipe_prev = pipe_fd[READ_IDX];

				/* Aguarda o comando terminar de executar
				   no processo filho para iniciar uma nova
				   iteração e executar o próximo comando do pipe
				*/
				waitpid(pid, &pid_stat, 0);
			}
		}
	}
	else { // valida a entrada e atualiza o "estado" do shell
		for(i=0; i < n_splits; i++) {
			rc = run(pipes[i], exec);
			if (rc < 0)
				return FAILURE;
		}
	}

	return SUCCESS;
}

static void print_login()
{
	if (shell.pout) {
		if (shell.style == STYLE_SEQ)
			fprintf(shell.pout, "%s seq> ", LOGIN);
		else
			fprintf(shell.pout, "%s par> ", LOGIN);
	}
}

void cmd_init(void *pin, void *pout)
{
	shell.style = STYLE_SEQ;
	shell.pin = (FILE *) pin;
	shell.pout = (FILE *) pout;
}

int cmd_read_ln(char *str)
{
	int rc = 0;

	print_login();
	if(shell.pin)
		rc = fscanf(shell.pin, " %[^\n]", str);

	return rc;
}

int cmd_execute(char *str)
{
	int i;
	int rc;
	int pid;
	int n_splits;
	FILE *pdata;
	int pids[CMD_LIMIT];
	char *cmds[CMD_LIMIT];
	int pids_stat[CMD_LIMIT];
	char *cmds_tmp[CMD_LIMIT];

	/*
		Quebra a linha em comandos a serem executados
		Ex1: "ls -l; ls" -> {"ls -l", "ls", NULL}
	*/
	n_splits = str_split(cmds, str, ";", CMD_LIMIT);

	for(i=0; i < n_splits; i++) {

		/*
			cria um processo pai e um filho e ambos os
			processos executam o código abaixo.
		*/
		pid = fork();

		if (pid == FAILURE) {
			fprintf(stderr, "Error: %s\n", strerror(errno));
		}
		else if (pid == CHILD) {
			pdata = NULL;

			// valida se é necessário redirecionar o arquivo
			if (str_split(cmds_tmp, cmds[i], ">>", CMD_LIMIT) == 2)
				pdata = redirect(cmds_tmp[1], "a");
			else if (str_split(cmds_tmp, cmds[i], ">", CMD_LIMIT) == 2)
				pdata = redirect(cmds_tmp[1], "w");
			else if (str_split(cmds_tmp, cmds[i], "<", CMD_LIMIT) == 2)
				pdata = redirect(cmds_tmp[1], "r");

			check_pipe_and_run(cmds[i], EXECUTE);

			if (pdata)
				fclose(pdata);

			return EXIT;
		}
		else {
			pids[i] = pid;
			/* Se estilo for sequencial espera até o processo filho
			   terminar de executar o comando antes de ir para o
			   próximo */
			if (shell.style == STYLE_SEQ)
				waitpid(pids[i], &pids_stat[i], 0);

			rc = check_pipe_and_run(cmds[i], NOT_EXECUTE);
			if (rc < 0)
				return FAILURE;
		}
	}

	/* Se estilo for paralelo espera até todos os comando terminarem de
	ser executados (processos filhos) antes de continuar e ler a próxima
	linha da entrada */
	for(i=0; shell.style == STYLE_PAR && i < n_splits; i++)
		waitpid(pids[i], &pids_stat[i], 0);

	return SUCCESS;
}
