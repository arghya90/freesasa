/*
  Copyright Simon Mitternacht 2013-2014.

  This file is part of Sasalib.
  
  Sasalib is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  Sasalib is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with Sasalib.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include "sasalib.h"
#include "srp.h"

#if STDC_HEADERS
extern int getopt(int, char * const *, const char *);
extern int optind;
#endif

char *program_name;

typedef struct  {
    sasalib_t *s;
    FILE *B;
    int per_residue;
} settings_t;

settings_t def_settings = {
    .s = NULL,
    .B = NULL,
    .per_residue = 0
};

void help() {
    fprintf(stderr,"\nUsage: %s [-hLSrn:d:t:p:B:] pdb-file(s)\n",
	    program_name);
    fprintf(stderr,
            "\nOptions are:\n"
            "       -h  print this message\n"
            "       -p  Probe radius. Default value is %4.2f Å.\n"
            "       -S  use Shrake & Rupley algorithm [default]\n"
            "       -L  use Lee & Richards algorithm\n"
            "       -d  grid spacing in Lee & Richards algorithm\n"
            "           Default value is %4.2f Å\n"
            ,SASALIB_DEF_PROBE_RADIUS,SASALIB_DEF_LR_D);
    fprintf(stderr,
            "       -n  number of test points in Shrake & Rupley algorithm\n"
            "           Default is %d, allowed values are:\n"
            "           ",SASALIB_DEF_SR_N);
    sasalib_srp_print_n_opt(stderr);
#ifdef HAVE_LIBPTHREAD
    fprintf(stderr,
            "       -t  number of threads to use in calculation (for multicore CPUs)\n");
#endif    
    fprintf(stderr,
            "       -r  print SASA for each residue\n");
    fprintf(stderr,
            "       -B  print PDB file with SASA for each atom as B-factors,\n" 
            "           in specified output file.");
    fprintf(stderr,
            "\nIf no pdb-file is specified STDIN is used for input.\n\n");
}

void short_help() {
    fprintf(stderr,"Run '%s -h' for usage instructions.\n",
	    program_name);
}

void run_analysis(FILE *input, const char *name, const settings_t *settings) {
    sasalib_t *s = sasalib_init();
    double tmp;
    sasalib_copy_param(s,settings->s);
    sasalib_set_proteinname(s,name); 
    if (sasalib_calc_pdb(s,input) == SASALIB_FAIL) {
        fprintf(stderr,"%s: error: Invalid input. Aborting.\n",
                program_name);
        exit(EXIT_FAILURE);
    }
    sasalib_log(stdout,s);
    printf("\nTotal:  %9.2f A2\nPolar:  %9.2f A2\nApolar: %9.2f A2\n",
           sasalib_area_total(s), sasalib_area_class(s,SASALIB_POLAR),
           sasalib_area_class(s, SASALIB_APOLAR));
    if ((tmp = sasalib_area_class(s, SASALIB_NUCLEICACID)) > 0) 
        printf("Nucleic: %9.2f A2\n",tmp);
    if ((tmp = sasalib_area_class(s, SASALIB_CLASS_UNKNOWN)) > 0) 
        printf("Unknown: %9.2f A2\n",tmp);
    if (settings->per_residue) { 
        printf("\n");
        sasalib_per_residue(stdout,s);
    }
    if (settings->B) sasalib_write_pdb(settings->B,s);
    sasalib_free(s);
}

int main (int argc, char **argv) {
    int alg_set = 0;
    FILE *input = NULL;
    settings_t settings = def_settings;
    settings.s = sasalib_init();
    extern char *optarg;
    char opt;
#ifdef PROGRAM_NAME
    program_name = PROGRAM_NAME;
#else
    program_name = argv[0];
#endif 

    while ((opt = getopt(argc, argv, "n:d:t:p:B:hLSr")) != -1) {
	errno = 0;
	int result = SASALIB_SUCCESS;
        switch(opt) {
	case 'h':
	    help();
	    exit(EXIT_SUCCESS);
	case 'B':
	    settings.B = fopen(optarg, "w");
	    if (settings.B == NULL) {
            fprintf(stderr,"%s: error: could not open file '%s'; %s\n",
                    program_name,optarg,strerror(errno));
            short_help();
            exit(EXIT_FAILURE);
	    }
	    break;
	case 'n':
	    result = sasalib_set_sr_points(settings.s,atoi(optarg));
	    break;
	case 'S':
	    result = sasalib_set_algorithm(settings.s,SASALIB_SHRAKE_RUPLEY);
	    ++alg_set;
	    break;
	case 'L':
	    result = sasalib_set_algorithm(settings.s,SASALIB_LEE_RICHARDS);
	    ++alg_set;
	    break;
	case 'd':
	    result = sasalib_set_lr_delta(settings.s,atof(optarg));
	    break;
	case 'p':
	    result = sasalib_set_probe_radius(settings.s,atof(optarg));
	    break;
	case 'r':
	    settings.per_residue = 1;
	    break;
	case 't':
#if HAVE_LIBPTHREAD	    
	    result = sasalib_set_nthreads(settings.s,atoi(optarg));
#else 
	    fprintf(stderr, "%s: warning: option 't' only defined if program"
		    " compiled with thread support.\n",
		    program_name);
#endif
	    break;
	default:
	    fprintf(stderr, "%s: warning: unknown option '%c' (will be ignored)\n", 
                program_name,opt);
	    break;
        }
        if (result == SASALIB_FAIL) {
	    fprintf(stderr, "%s: error: failed to start SASA calculation with "
                "provided options. Aborting", 
                program_name);
	    if (errno != 0) fprintf(stderr,"; %s\n",strerror(errno));
	    else fprintf(stderr,".\n");
	    exit(EXIT_FAILURE);
	} else if (result == SASALIB_WARN) {
            fprintf(stderr, "%s: warning: calculations might not "
                    "correspond to specification or results might "
                    "be unreliable (see warnings).\n", 
                    program_name);
        }
    }
    if (alg_set > 1) {
        fprintf(stderr, "%s: error: multiple algorithms specified.\n",
                program_name);
        exit(EXIT_FAILURE);
    }
    
    if (argc > optind) {
        for (int i = optind; i < argc; ++i) {
            errno = 0;
            input = fopen(argv[i],"r");
            if (input != NULL) {
                run_analysis(input,argv[i],&settings);
                fclose(input);
            } else {
                fprintf(stderr, "%s: error opening file '%s'; %s\n", 
                        program_name,argv[i],strerror(errno));
                exit(EXIT_FAILURE);
            }	    
        }
    } else {
        if (!isatty(STDIN_FILENO)) {
            run_analysis(stdin,"stdin",&settings);
        } else {
            fprintf(stderr,"%s: no input.\n",
                    program_name);
        }
    }    
    
    sasalib_free(settings.s);
    if (settings.B) fclose(settings.B);
    return EXIT_SUCCESS;
}
