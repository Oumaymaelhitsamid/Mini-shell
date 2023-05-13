/*****************************************************
 * Copyright Grégory Mounié 2008-2015                *
 *           Simon Nieuviarts 2002-2009              *
 * This code is distributed under the GLPv3 licence. *
 * Ce code est distribué sous la licence GPLv3+.     *
 *****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <signal.h>

#include "variante.h"
#include "readcmd.h"

#ifndef VARIANTE
#error "Variante non défini !!"
#endif

struct bg_proc {
	pid_t pid;
	char* commande;
	struct bg_proc* suiv;
};

void affiche_commande(struct bg_proc* proc) {
	if (proc->pid) {
        printf("%s ", proc->commande);
	} else {
		printf("ensishell");
	}
}

void affiche(struct bg_proc* proc) {
	printf("|[%u]      \t", proc->pid);
	affiche_commande(proc);
	printf("\n");
}

char* get_commande(char** cmd) {
	int total_len = 0;
	int i = 0;
	for (i = 0; cmd[i]; i++) {total_len += strlen(cmd[i]);}

	char* res = malloc((total_len + i) * sizeof(char));
	int taille = 0;

	for (int i = 0; cmd[i]; i++) {
		strcpy(res + taille, cmd[i]);
		taille += strlen(cmd[i]) + 1;;
		res[taille-1] = ' ';
	}
	return res;
}

void jobs(struct bg_proc* liste) {
	printf("+-- Processus actuels : --+\n+=========================+\n");
	struct bg_proc* actu = liste; affiche(actu);
	struct bg_proc* next = liste->suiv;
	while (next != NULL) {
		if (waitpid(next->pid, NULL, WNOHANG)) {		// Le processus est terminé, on l'enlève de la liste et on ne l'affiche pas
			actu->suiv = actu->suiv->suiv;
			free(next);
			next = actu->suiv;
		} else {
			affiche(next);
			actu = actu->suiv; next = next->suiv;
		}
	}
	printf("+-------------------------+\n");
}

void free_liste(struct bg_proc* liste) {
	struct bg_proc* actu = liste;
	for (struct bg_proc* i = liste->suiv; i != NULL; i = i->suiv) {
		free(actu->commande);
		free(actu);
		actu = i;
	}
	free(actu);
}

void terminaison() {
		int status;
		pid_t pid;

		while (1) {
			pid = wait3(&status, WNOHANG, (struct rusage *)NULL );
			if (pid == 0)
				return;
			else if (pid == -1)
				return;
			else
				printf ("\rProcessus [%u] terminé\n", pid);
		}
}


void executer(struct cmdline *l, struct bg_proc* bg_liste, struct rlimit *rlim, int is_scheme) {
	/* Crée le processus qui va executer la commande */
	pid_t attendu = 0;

	attendu = fork();

	if (attendu) {	// Ici on est le père donc l'execution du shell
		if (!l->bg) { 	// Execution normale
			printf("On attend %u\n", attendu);
			waitpid(attendu, NULL, 0);

		} else {		// Execution en backgroud
			signal(SIGCHLD, terminaison);
			struct bg_proc* cell = malloc(sizeof(struct bg_proc));
			cell->pid = attendu; cell->commande = get_commande(l->seq[0]); cell->suiv = bg_liste->suiv;
			bg_liste->suiv = cell;

			if (is_scheme) {
				free(cell);
			}

		}	
	} else {		// Ici on est dans le fils donc la commande

		if (rlim) {setrlimit(RLIMIT_CPU, rlim);}

		int in, out;

		if (l->in) {
			in = open(l->in, O_RDONLY);
		
			dup2(in, STDIN_FILENO);
			close(in);
		}

		if (l->out) {
			out = open(l->out, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
		
			dup2(out, STDOUT_FILENO);
			close(out);
		}

		// Si on a un pipe (et donc l->seq[1] != NULL), 
		// il faut créer des sous fils pour chacune des sous-commandes successives

		// On se concentre ici au cas avec 2 commandes successives seulement
		if (l->seq[1]) {

			int pipefd[2];
			if (pipe(pipefd) == -1) {exit(EXIT_FAILURE);}
			
			if (fork()) {		// Le petit fils qui execute la 1ere commande
				char **cmd = l->seq[0];

				dup2(pipefd[1], STDOUT_FILENO);		// On remplace stdout par l'entrée du pipe
				close(pipefd[0]);					// On verrouille le pipe dans ce thread
				close(pipefd[1]);

				execvp(cmd[0], cmd);	// On execute la commande

				exit(EXIT_SUCCESS);

			}
			// Le fils qui execute la 2eme commande
			char** cmd = l->seq[1];

			dup2(pipefd[0], STDIN_FILENO);	// On remplace stdin par la sortie du pipe
			close(pipefd[0]);				// On verrouille le pipe dans ce thread
			close(pipefd[1]);

			execvp(cmd[0], cmd);

			exit(EXIT_SUCCESS);

			// On verouille le pipe et on attend que le fils ai terminé
			wait(NULL);
			
		} else {

			// Sinon on execute normalement
			char **cmd = l->seq[0];
			execvp(cmd[0], cmd);
		}

		exit(EXIT_SUCCESS);
	}	
}


void terminate(char *line) {
#if USE_GNU_READLINE == 1
	/* rl_clear_history() does not exist yet in centOS 6 */
	clear_history();
#endif
	if (line)
	  free(line);
	printf("exit\n");
	exit(0);
}


/* Guile (1.8 and 2.0) is auto-detected by cmake */
/* To disable Scheme interpreter (Guile support), comment the
 * following lines.  You may also have to comment related pkg-config
 * lines in CMakeLists.txt.
 */

#if USE_GUILE == 1
#include <libguile.h>

int question6_executer(char *line)
{
	/* Question 6: Insert your code to execute the command line
	 * identically to the standard execution scheme:
	 * parsecmd, then fork+execvp, for a single command.
	 * pipe and i/o redirection are not required.
	 */
	if (line == 0 || !strncmp(line, "exit", 4)) {
		terminate(line);
	}

	struct cmdline* l = parsecmd( & line);

	if (!l) {
		terminate(0);
	}

	if (l->err) {
		/* Syntax error, read another command */
		printf("error: %s\n", l->err);
		return 1;
	}

	executer(l, NULL, NULL, 1);
	
	return 0;
}

SCM executer_wrapper(SCM x)
{
        return scm_from_int(question6_executer(scm_to_locale_stringn(x, 0)));
}
#endif


int main() {
        printf("Variante %d: %s \t\t(guile state : %u)\n", VARIANTE, VARIANTE_STRING, USE_GUILE);
		struct bg_proc* bg_liste = malloc(sizeof(struct bg_proc));
		bg_liste->pid = 0; bg_liste->commande = NULL; bg_liste->suiv = NULL;

		struct rlimit *rlim = NULL;

#if USE_GUILE == 1
        scm_init_guile();
        /* register "executer" function in scheme */
        scm_c_define_gsubr("executer", 1, 0, 0, executer_wrapper);
#endif

	while (1) {
		struct cmdline *l;
		char *line=0;
		char *prompt = "ensishell>";
		int i, j;

		/* Readline use some internal memory structure that
		   can not be cleaned at the end of the program. Thus
		   one memory leak per command seems unavoidable yet */
		line = readline(prompt);
		if (line == 0 || !strncmp(line, "exit", 4)) {
			if (bg_liste) free_liste(bg_liste);
			if (rlim) free(rlim);
			terminate(line);
		}
		if (!strncmp(line, "jobs", 4)) {
			jobs(bg_liste);
			free(line); 
			continue;
		}


#if USE_GNU_READLINE == 1
		add_history(line);
#endif


#if USE_GUILE == 1
		/* The line is a scheme command */
		if (line[0] == '(') {
			char catchligne[strlen(line) + 256];
			sprintf(catchligne, "(catch #t (lambda () %s) (lambda (key . parameters) (display \"mauvaise expression/bug en scheme\n\")))", line);
			scm_eval_string(scm_from_locale_string(catchligne));
			free(line);
            continue;
        }
#endif

		/* parsecmd free line and set it up to 0 */
		l = parsecmd( & line);

		/* If input stream closed, normal termination */
		if (!l) {
			terminate(0);
		}
		
		if (l->err) {
			/* Syntax error, read another command */
			printf("error: %s\n", l->err);
			continue;
		}
		
		if (!strncmp(l->seq[0][0], "ulimit", 6)) {
			if (rlim) free(rlim);
			rlim = malloc(sizeof(struct rlimit));
			rlim->rlim_cur = atoi(l->seq[0][1]);
			rlim->rlim_max = rlim->rlim_cur +5;
		}

		if (l->in) printf("in: %s\n", l->in);
		if (l->out) printf("out: %s\n", l->out);
		if (l->bg) printf("background (&)\n");

		for (i=0; l->seq[i]!=0; i++) {
			char **cmd = l->seq[i];
			printf("seq[%d]: ", i);
                        for (j=0; cmd[j]!=0; j++) {
                                printf("'%s' ", cmd[j]);
                        }
			printf("\n");
		}

		executer(l, bg_liste, rlim, 0);
	}
}

