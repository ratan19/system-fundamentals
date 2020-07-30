#include <stdlib.h>
#include <errno.h>

#include "jobber.h"

#include <string.h>
#include <ctype.h>

#include "../include/debug.h"
#include "../include/rhelper.h"


/*
 * "Jobber" job spooler.
 */

int main(int argc, char *argv[])
{

	while(1){

		char *input = sf_readline("jobber>");

		process(input);
	}
    // exit(EXIT_FAILURE);
}

/*
 * Just a reminder: All non-main functions should
 * be in another file not named main.c
 */
