#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#include <iostream>
#include "data_parser.hpp"

char help[] = " -a <address> [-t] [-o <filename>] [-d] [-q]\n"\
			   "\t-h: This help\n"\
			   "\t-a <address>: Set the address of the B41T meter, eg, -a 98:84:E3:CD:C0:E5\n"\
			   "\t-t: Generate a text file containing current meter data (default to owon.txt)\n"\
			   "\t-o <filename>: Set the filename for the meter data ( overrides 'owon.txt' )\n"\
			   "\t-l <filename>: Set logging and the filename for the log\n"\
			   "\t-d: debug enabled\n"\
			   "\t-q: quiet output\n"\
			   "\n\n\texample: owoncli -a 98:84:E3:CD:C0:E5 -t -o obsdata.txt\n"\
			   "\n";

uint8_t OLs[] = { 0x2b, 0x3f, 0x30, 0x3a, 0x3f };

char default_output[] = "owon.txt";
uint8_t sigint_pressed;

struct glb {
	uint8_t debug;
	uint8_t quiet;
	uint8_t textfile_output;
	uint16_t flags;

	char *log_filename;
	char *output_filename;
	char *B41T_address;

};

int init( struct glb *g ) {
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->textfile_output = 0;

	g->output_filename = default_output;
	g->B41T_address = NULL;
	g->log_filename = NULL;

	return 0;
}

int parse_parameters( struct glb *g, int argc, char **argv ) {

	int i;

	for (i = 0; i < argc; i++) {

		if (argv[i][0] == '-') {

			/* parameter */
			switch (argv[i][1]) {
				case 'h':
					fprintf(stdout,"Usage: %s %s", argv[0], help);
					exit(1);
					break;

				case 'l':
					/* set the logging */
					i++;
					if (i < argc) g->log_filename = argv[i];
					else {
						fprintf(stderr,"Require log filename; -l <filename>\n");
						exit(1);
					}
					break;

				case 'a':
					/* set address of B41T*/
					i++;
					if (i < argc) g->B41T_address = argv[i];
					else {
						fprintf(stderr,"Insufficient parameters; -a <address>\n");
						exit(1);
					}
					break;

				case 'o':
					/* set output file for text */
					i++;
					if (i < argc) g->output_filename = argv[i];
					else {
						fprintf(stderr,"Insufficient parameters; -o <file>\n");
						exit(1);
					}
					break;

				case 't':
					g->textfile_output = 1;
					break;

				case  'd':
					g->debug = 1;
					break;

				case 'q':
					g->quiet = 1;
					/* separate output files */
					break;

				default:
					break;
			} // switch
		}
	}

	return 0;
}


void handle_sigint( int a ) {
	sigint_pressed = 1;
}

int main( int argc, char **argv ) {
	char cmd[1024];
	FILE *fp, *fo, *fl;
	struct glb g;

	fo = fp = fl = NULL;

	sigint_pressed = 0;
	signal(SIGINT, handle_sigint); 

	init( &g );
	parse_parameters( &g, argc, argv );

	/*
	 * Sanity check our parameters
	 */
	if (g.B41T_address == NULL) {
		fprintf(stderr, "Owon B41T bluetooth address required, try 'sudo hcitool lescan' to get address\n");
		exit(1);
	}

	/*
	 * All good... let's get on with the job
	 *
	 * First step, try to open a pipe to gattool so we
	 * can read the bluetooth output from the B41T via STDIN
	 *
	 */
	snprintf(cmd,sizeof(cmd), "gatttool -b %s --char-read --handle 0x2d --listen", g.B41T_address);
	fp = popen(cmd,"r");
	if (fp == NULL) {
		fprintf(stderr,"Error executing '%s'\n", cmd);
		exit(1);
	} else {
		if (!g.quiet) fprintf(stdout,"Success (%s)\n", cmd);
	}

	/*
	 * If required, open the log file, in append mode
	 *
	 */
	if (g.log_filename) {

		fl = fopen(g.log_filename, "a");
		if (fl == NULL) {
			fprintf(stderr,"Couldn't open '%s' file to write/append, NO LOGGING\n", g.log_filename);
		}

	}

	/*
	 * If required, open the text file we're going to generate the multimeter
	 * data in to, this is a single frame only data file it is NOT a log file
	 *
	 */
	if (g.textfile_output) {
		fo = fopen("owon.txt","w");
		if (fo == NULL) {
			fprintf(stderr,"Couldn't open owon.data file to write, not saving to file\n");
			g.textfile_output = 0;
		}
	}

	// variables to store the date and time components
    int hours, minutes, seconds, day, month, year;
    time_t now;


	/*
	 * Keep reading, interpreting and converting data until someone
	 * presses ctrl-c or there's an error
	 */
	while (fgets(cmd, sizeof(cmd), fp) != NULL) {
		char *p, *q;
		uint8_t d[14];
		uint8_t i = 0;

		if (sigint_pressed) {
			if (fo) fclose(fo);
			if (fp) pclose(fp);
			fprintf(stdout, "Exit requested\n");
			fflush(stdout);
			exit(1);
		}

		if (g.debug) fprintf(stdout,"%s", cmd);

		p = strstr(cmd, "2e value: ");
		if (!p) continue;

		if (p) p += sizeof("2e value:");
		if (strlen(p) != 19) {
			if (!g.quiet) {
				fprintf(stdout, "\33[2K\r"); // line erase
				fprintf(stdout,"Waiting...");
			}
			continue;
		}
		while ( p && *p != '\0' && (i < 14) ) {
			d[i] = strtol(p, &q, 16);
			if (!q) break;
			p = q;
			if (g.debug)	fprintf(stdout,"[%d](%02x) ", i, d[i]);
			i++;
		}
		if (g.debug) fprintf(stdout,"\n");

		std::vector<uint8_t> dataVec;
		dataVec.insert(dataVec.end(), &d[0], &d[6]);

		data_parser dp(dataVec);

    	std::string str = dp.formattedString();

    	char char_array[str.length()];
    	strcpy(char_array, str.c_str());

    	// printf("%s\n", char_array);

    	// void format_time(char *output){


		if (g.textfile_output) {
			rewind(fo);
			fprintf(fo, "%s%c", cmd, 0);
			fflush(fo);
		}

		if (g.log_filename && fl) {
	
		    time(&now);
		 
		    struct tm *local = localtime(&now);
		 
		    hours = local->tm_hour;         // get hours since midnight (0-23)
		    minutes = local->tm_min;        // get minutes passed after the hour (0-59)
		    seconds = local->tm_sec;        // get seconds passed after a minute (0-59)
		 
		    day = local->tm_mday;            // get day of month (1 to 31)
		    month = local->tm_mon + 1;      // get month of year (0 to 11)
		    year = local->tm_year + 1900;   // get year since 1900
		 
		    const time_t epoch_plus_11h = 60 * 60 * 11;
			const int local_time = localtime(&epoch_plus_11h)->tm_hour;
			const int gm_time = gmtime(&epoch_plus_11h)->tm_hour;
			const int tz_diff = local_time - gm_time;

			fprintf(fl, "[%02d/%02d/%d:%02d:%02d:%02d %d] %s\n",
					day, month, year, hours, minutes, seconds, tz_diff
					, char_array
					);
			fflush(fl);
		}

		if (!g.quiet) {
			fprintf(stdout, "\33[2K\r"); // line erase
			fprintf(stdout, "\x1B[A"); // line up
			fprintf(stdout, "\33[2K\r"); // line erase
			if (g.debug) fprintf(stdout,"[ %03d %03d %03d %03d %03d %03d ]", d[6], d[7], d[8], d[9], d[10], d[11]);
			fprintf(stdout,"%s\n", char_array );
			fflush(stdout);
		}

	}

	if (pclose(fp)) {
		fprintf(stdout,"Command not found, or exited with error\n");
		if (fl) fclose(fl);
		if (fo) fclose(fo);
	}
	return 0;
}
