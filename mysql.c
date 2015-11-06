#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <pthread.h>

#include <mysql/mysql.h>

int verbose = 0;
int total = 0;
volatile int quit = 0;

pthread_mutex_t mutex_pass =  PTHREAD_MUTEX_INITIALIZER;

struct args {
	char *host;
	char *db;
	int port;
};

void print_help(FILE *fp, char *app) {
	fprintf(fp, "Usage: %s [<options>]\n", app);
	fprintf(fp, "\n");
	fprintf(fp, "     -h          Print this help and exit\n");
	fprintf(fp, "     -v          Verbose. Repeat for more info\n");
	fprintf(fp, "     -t <host>   host to try\n");
	fprintf(fp, "     -p <port>   port to connect on\n");
	fprintf(fp, "     -n <num>    number of threads to use\n");
	fprintf(fp, "\n");
	fprintf(fp, "Note: usernames / password will be read from stdin\n");
	fprintf(fp, "The format for this is username:password\n");
	fprintf(fp, "\n");
}

int try(char *hostname, char *username, char *password, char *db, int port) {
	MYSQL mysql;
	mysql_init(&mysql);

	if (!mysql_real_connect(&mysql, hostname, username, password, db, port, NULL, 0)) {
		switch(mysql_errno(&mysql)) {
			case 1045: /* ER_ACCESS_DENIED_ERROR */
				if (verbose >= 1)
					printf("Failed: %d %s\n", mysql_errno(&mysql), mysql_error(&mysql));
				break;
			default:
				printf("Unknown Error: %d -> %s\n",  mysql_errno(&mysql),  mysql_error(&mysql));
				break;
		}
		return 0;
	}
	
	if (verbose >= 1)	
		printf("Success: %d %s\n", mysql_errno(&mysql), mysql_error(&mysql));

	mysql_close(&mysql);
	return 1;
}

int getpassword(char **buf, size_t *buflen, char **username, char **password) {

	pthread_mutex_lock(&mutex_pass);

	if (getline(buf, buflen, stdin) >= 0) {
		pthread_mutex_unlock(&mutex_pass);
		char *tmp = strchr(*buf, ':');
		if (tmp == 0 || tmp[1] == 0)
			return 0;
		*username = *buf;
		*tmp = 0;
		tmp++;
		*password = tmp;
		tmp = strchr(*password, '\n');
		if (tmp != 0)
			*tmp = 0;
		if (verbose >= 2)
			printf("username: %s password: %s\n", *username, *password);
		return 1;
	}

	pthread_mutex_unlock(&mutex_pass);
	return 0;
}

void *run(void *p) {
	struct args *a = (struct args *) p;
	char *buf = 0;
	size_t buflen = 0;
	char *user = 0;
	char *pass = 0;

	while(quit == 0) {
		if (getpassword(&buf, &buflen, &user, &pass) == 0)
			goto free; /* we ran out of passwords */

		if (try(a->host, user, pass, a->db, a->port)) {
			printf("Success! Username: %s Password: %s\n", user, pass);
			quit = 1;
			goto free;
		}
	}

free:
	if (buf != NULL)
		free(buf);

	pthread_exit(NULL);
	return NULL;
}

int main(int argc, char **argv) {
	struct args args;
	pthread_t *thd;
	pthread_attr_t attr;
	int nthreads = 1;
	int i = 0;
	int c;

	memset(&args, 0, sizeof(args));

	while( (c = getopt(argc, argv, "d:hn:p:t:v")) != -1) {
		switch(c) {
			case 'd':
				args.db = optarg;
				break;
			case 'h':
				print_help(stdout, argv[0]);
				exit(EXIT_SUCCESS);
				break;
			case 'n':
				nthreads = atoi(optarg);
				break;
			case 't':
				args.host = optarg;
				break;
			case 'v':
				verbose++;
				break;
			case 'p':
				args.port = atoi(optarg);
				break;
		}
	}
	
	if (args.db == NULL)
		args.db = "mysql";

	if (args.host == NULL)
		args.host = "localhost";

	thd = malloc(nthreads * sizeof(*thd));
	if (!thd) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	
	mysql_library_init(0, NULL, NULL);	

	if (pthread_attr_init(&attr) != 0) {
		perror("pthread_attr_init");
		exit(EXIT_FAILURE);
	}

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0) {
		perror("pthread_attr_setdetachstate");
		exit(EXIT_FAILURE);
	}

	for(i=0;i<nthreads;i++) {
		if (pthread_create(&thd[i], NULL, run, &args) != 0) {
			perror("pthread_create");
			exit(EXIT_FAILURE);
		}
	}

	for(i=0;i<nthreads;i++) {
		if (pthread_join(thd[i], NULL) != 0) {
			perror("pthread_join");
			exit(EXIT_FAILURE);
		}
	}

	pthread_attr_destroy(&attr);

	free(thd);	

	mysql_library_end();
	return EXIT_SUCCESS;
}
