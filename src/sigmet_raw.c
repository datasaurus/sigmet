/*
 -	sigmet_raw.c --
 -		Client to sigmet_rawd. See sigmet_raw (1).
 -
 .	Copyright (c) 2009 Gordon D. Carrie
 .	All rights reserved.
 .
 .	Please send feedback to dev0@trekix.net
 .
 .	$Revision: $ $Date: $
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
    char *argv1 = "/home/gcarrie/.sigmet_raw/rslt";
    char *argv2 = "types";
    size_t l;
    char nul = '\0';
    FILE *cmd_pipe, *rslt;
    int c;			/* Character from server */

    /* Open command pipe */
    if ( !(cmd_pipe = fopen("/home/gcarrie/.sigmet_raw/sigmet.in", "w")) ) {
	perror("could not open command pipe");
	exit(EXIT_FAILURE);
    }

    /* Send command */
    l = strlen(argv1) + 1 + strlen(argv2) + 1;
    if ( fwrite(&l, sizeof(size_t), 1, cmd_pipe) != 1 ) {
	perror("could not write command size");
	exit(EXIT_FAILURE);
    }
    if (fprintf(cmd_pipe, "%s", argv1) != strlen(argv1)) {
	perror("could not write first command");
	exit(EXIT_FAILURE);
    }
    if ( fwrite(&nul, 1, 1, cmd_pipe) != 1 ) {
	perror("could not write first nul");
	exit(EXIT_FAILURE);
    }
    if (fprintf(cmd_pipe, "%s", argv2) != strlen(argv2)) {
	perror("could not write second command");
	exit(EXIT_FAILURE);
    }
    if ( fwrite(&nul, 1, 1, cmd_pipe) != 1 ) {
	perror("could not write second nul");
	exit(EXIT_FAILURE);
    }
    l = 0;
    if ( fwrite(&l, sizeof(size_t), 1, cmd_pipe) != 1 ) {
	perror("could not write command size");
	exit(EXIT_FAILURE);
    }

    /* Close command pipe. Open result pipe. */
    if ( fclose(cmd_pipe) == EOF ) {
	perror("could not close command pipe");
    }
    if ( mkfifo(argv1, 0600) == -1 ) {
	perror("could not create result pipe");
	exit(EXIT_FAILURE);
    }
    if ( !(rslt = fopen(argv1, "r")) ) {
	perror("could not open result pipe");
	exit(EXIT_FAILURE);
    }

    /* Get and send result */
    while ( (c = fgetc(rslt)) != EOF ) {
	putchar(c);
    }

    /* Close and remove result pipe. */
    if ( fclose(rslt) == EOF ) {
	perror("could not close result pipe");
    }
    if ( unlink(argv1) == -1 ) {
	perror("could not remove result pipe");
    }

    return 0;
}
