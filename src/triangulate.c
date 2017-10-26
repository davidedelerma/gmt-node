 /*--------------------------------------------------------------------
 *	$Id: triangulate.c 18282 2017-05-30 01:57:05Z pwessel $
 *
 *	Copyright (c) 1991-2017 by P. Wessel, W. H. F. Smith, R. Scharroo, J. Luis and F. Wobbe
 *	See LICENSE.TXT file for copying and redistribution conditions.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU Lesser General Public License as published by
 *	the Free Software Foundation; version 3 or any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU Lesser General Public License for more details.
 *
 *	Contact info: gmt.soest.hawaii.edu
 *--------------------------------------------------------------------*/
/*
 * Brief synopsis: triangulate reads one or more files (or stdin) with x,y[,whatever] and
 * outputs the indices of the vertices of the optimal Delaunay triangulation
 * using the method by Watson, D. F., ACORD: Automatic contouring of raw data,
 * Computers & Geosciences, 8, 97-101, 1982.  Optionally, the output may take
 * the form of (1) a multi-segment file with the vertex coordinates needed to
 * draw the triangles, or (2) a grid file based on gridding the plane estimates.
 * PS. Instead of Watson's method you may choose to link with the triangulate
 * routine written by Jonathan Shewchuk.  See the file TRIANGLE.HOWTO for
 * details.  That function is far faster than Watson's method and also allows
 * for Voronoi polygon output.
 *
 * Author:	Paul Wessel
 * Date:	1-JAN-2010
 * Version:	5 API
 */
 
#include "gmt_dev.h"

#define THIS_MODULE_NAME	"triangulate"
#define THIS_MODULE_LIB		"core"
#define THIS_MODULE_PURPOSE	"Optimal (Delaunay) triangulation and gridding of Cartesian table data"
#define THIS_MODULE_KEYS	"<D{,>D},GG)"
#define THIS_MODULE_NEEDS	"r"
#define THIS_MODULE_OPTIONS "-:>JRVbdefhirs" GMT_OPT("Hm")

struct TRIANGULATE_CTRL {
	struct Out {	/* -> */
		bool active;
		char *file;
	} Out;
	struct D {	/* -Dx|y */
		bool active;
		unsigned int dir;
	} D;
	struct E {	/* -E<value> */
		bool active;
		double value;
	} E;
	struct F {	/* -F<pregrid>[+d] */
		bool active;
		char *file;
		unsigned int mode;
	} F;
	struct G {	/* -G<output_grdfile> */
		bool active;
		char *file;
	} G;
	struct M {	/* -M */
		bool active;
	} M;
	struct N {	/* -N */
		bool active;
	} N;
	struct Q {	/* -Q[n] */
		bool active;
		unsigned int mode;
	} Q;
	struct S {	/* -S */
		bool active;
	} S;
	struct T {	/* -T */
		bool active;
	} T;
	struct Z {	/* -Z */
		bool active;
	} Z;
};

struct TRIANGULATE_EDGE {
	unsigned int begin, end;
};

GMT_LOCAL int compare_edge (const void *p1, const void *p2) {
	const struct TRIANGULATE_EDGE *a = p1, *b = p2;

	if (a->begin < b->begin) return (-1);
	if (a->begin > b->begin) return (+1);
	if (a->end < b->end) return (-1);
	if (a->end > b->end) return (+1);
	return (0);
}

GMT_LOCAL void *New_Ctrl (struct GMT_CTRL *GMT) {	/* Allocate and initialize a new control structure */
	struct TRIANGULATE_CTRL *C = NULL;
	
	C = gmt_M_memory (GMT, NULL, 1, struct TRIANGULATE_CTRL);
	
	/* Initialize values whose defaults are not 0/false/NULL */
	C->D.dir = 2;	/* No derivatives */
	return (C);
}

GMT_LOCAL void Free_Ctrl (struct GMT_CTRL *GMT, struct TRIANGULATE_CTRL *C) {	/* Deallocate control structure */
	if (!C) return;
	gmt_M_str_free (C->F.file);	
	gmt_M_str_free (C->G.file);	
	gmt_M_free (GMT, C);	
}

GMT_LOCAL int usage (struct GMTAPI_CTRL *API, int level) {
	gmt_show_name_and_purpose (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_PURPOSE);
	if (level == GMT_MODULE_PURPOSE) return (GMT_NOERROR);
	GMT_Message (API, GMT_TIME_NONE, "usage: triangulate [<table>] [-Dx|y] [-E<empty>] [-G<outgrid>]\n");
#ifdef NNN_MODE
	GMT_Message (API, GMT_TIME_NONE, "\t[%s] [%s] [-M] [-N] [-Q[n]]\n", GMT_I_OPT, GMT_J_OPT);
#else
	GMT_Message (API, GMT_TIME_NONE, "\t[%s] [%s] [-M] [-N] [-Q]\n", GMT_I_OPT, GMT_J_OPT);
#endif
	GMT_Message (API, GMT_TIME_NONE, "\t[%s] [-S] [-T] [%s] [-Z] [%s] [%s]\n\t[%s] [%s] [%s]\n\t[%s] [%s] [%s] [%s]\n\n",
		GMT_Rgeo_OPT, GMT_V_OPT, GMT_b_OPT, GMT_d_OPT, GMT_e_OPT, GMT_f_OPT, GMT_h_OPT, GMT_i_OPT, GMT_r_OPT, GMT_s_OPT, GMT_colon_OPT);

	if (level == GMT_SYNOPSIS) return (GMT_MODULE_SYNOPSIS);

	GMT_Message (API, GMT_TIME_NONE, "\tOPTIONS:\n");
	GMT_Option (API, "<");   
	GMT_Message (API, GMT_TIME_NONE, "\t-D Take derivative in the x- or y-direction (only with -G) [Default is z value].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-E Value to use for empty nodes [Default is NaN].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-G Grid data. Give name of output grid file and specify -R -I.\n");
#ifdef NNN_MODE
	GMT_Message (API, GMT_TIME_NONE, "\t   Use -Qn for natural nearest neighbors [Default is linear triangulation]\n");
#endif
	GMT_Message (API, GMT_TIME_NONE, "\t   Cannot be combined with -N.\n");
	GMT_Option (API, "I,J-");   
	GMT_Message (API, GMT_TIME_NONE, "\t-M Output triangle edges as multiple segments separated by segment headers.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   [Default is to output the indices of vertices for each Delaunay triangle].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-N Write indices of vertices to stdout when -G is used [only write the grid].\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-Q Compute Voronoi polygon edges instead (requires -R and Shewchuk algorithm) [Delaunay triangulation].\n");
#ifdef NNN_MODE
	GMT_Message (API, GMT_TIME_NONE, "\t   Append n to produce closed Voronoi polygons.\n");
#endif
	GMT_Message (API, GMT_TIME_NONE, "\t-S Output triangle polygons as multiple segments separated by segment headers.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Cannot be used with -Q.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-T Output triangles or polygons even if gridding has been selected with -G.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t   Default behavior is to produce a grid based on the triangles or polygons only.\n");
	GMT_Message (API, GMT_TIME_NONE, "\t-Z Expect (x,y,z) data on input (and output); automatically set if -G is used [Expect (x,y) data].\n");
	GMT_Option (API, "R,V,bi2");
	GMT_Message (API, GMT_TIME_NONE, "\t-bo Write binary (double) index table [Default is ASCII i/o].\n");
	GMT_Option (API, "d,e,f,h,i,r,s,:,.");
	
	return (GMT_MODULE_USAGE);
}

GMT_LOCAL int parse (struct GMT_CTRL *GMT, struct TRIANGULATE_CTRL *Ctrl, struct GMT_OPTION *options) {
	/* This parses the options provided to triangulate and sets parameters in CTRL.
	 * Any GMT common options will override values set previously by other commands.
	 * It also replaces any file names specified as input or output with the data ID
	 * returned when registering these sources/destinations with the API.
	 */

	unsigned int n_errors = 0, n_files = 0;
	char *c = NULL;
	struct GMT_OPTION *opt = NULL;
	struct GMTAPI_CTRL *API = GMT->parent;

	for (opt = options; opt; opt = opt->next) {
		switch (opt->option) {

			case '<':	/* Skip input files */
				if (!gmt_check_filearg (GMT, '<', opt->arg, GMT_IN, GMT_IS_DATASET)) n_errors++;
				break;
			case '>':	/* Got named output file */
				if (n_files++ == 0 && gmt_check_filearg (GMT, '>', opt->arg, GMT_OUT, GMT_IS_DATASET))
					Ctrl->Out.file = strdup (opt->arg);
				else
					n_errors++;
				break;

			/* Processes program-specific parameters */

			case 'D':
				Ctrl->D.active = true;
				switch (opt->arg[0]) {
					case 'x': case 'X':
						Ctrl->D.dir = GMT_X; break;
					case 'y': case 'Y':
						Ctrl->D.dir = GMT_Y; break;
					default:
						GMT_Report (API, GMT_MSG_NORMAL, "Syntax error: Give -Dx or -Dy\n");
						n_errors++; break;
				}
				break;
			case 'E':
				Ctrl->E.active = true;
				Ctrl->E.value = (opt->arg[0] == 'N' || opt->arg[0] == 'n') ? GMT->session.d_NaN : atof (opt->arg);
				break;
			case 'F':	/* Previous grid input values used */
				if (gmt_M_compat_check (GMT, 4) && opt->arg[0] == 0) {	/* Old -F instead of -r */
					GMT_Report (GMT->parent, GMT_MSG_COMPAT, "Warning: Option -F is deprecated. Use -r instead.\n" );
					n_errors += gmt_parse_common_options (GMT, "r", 'r', "");
					break;
				}
				GMT_Report (API, GMT_MSG_NORMAL, "Warning: -F is experimental and unstable.\n");
				if ((c = strstr (opt->arg, "+d"))) {	/* Got modifier to also use input data */
					c[0] = '\0';	/* Temporarily chop off modifier */
					Ctrl->F.mode = 1;
				}
				if ((Ctrl->F.active = gmt_check_filearg (GMT, 'F', opt->arg, GMT_IN, GMT_IS_GRID)) != 0)
					Ctrl->F.file = strdup (opt->arg);
				else
					n_errors++;
				if (c) c[0] = '+';	/* Restore chopped off modifier */
				break;
			case 'G':
				if ((Ctrl->G.active = gmt_check_filearg (GMT, 'G', opt->arg, GMT_OUT, GMT_IS_GRID)) != 0)
					Ctrl->G.file = strdup (opt->arg);
				else
					n_errors++;
				break;
			case 'I':
				n_errors += gmt_parse_inc_option (GMT, 'I', opt->arg);
				break;
			case 'm':
				if (gmt_M_compat_check (GMT, 4)) /* Warn and fall through */
					GMT_Report (API, GMT_MSG_COMPAT, "Warning: -m option is deprecated and reverted back to -M.\n");
				else {
					n_errors += gmt_default_error (GMT, opt->option);
					break;
				}
			case 'M':
				Ctrl->M.active = true;
				break;
			case 'N':
				Ctrl->N.active = true;
				break;
			case 'Q':
				Ctrl->Q.active = true;
				if (strchr (opt->arg, 'n')) {
					GMT_Report (API, GMT_MSG_NORMAL, "Warning: -Qn is experimental and unstable.\n");
					Ctrl->Q.mode |= 1;
				}
				break;
			case 'S':
				Ctrl->S.active = true;
				break;
			case 'T':
				Ctrl->T.active = true;
				break;
			case 'Z':
				Ctrl->Z.active = true;
				break;

			default:	/* Report bad options */
				n_errors += gmt_default_error (GMT, opt->option);
				break;
		}
	}

	n_errors += gmt_add_R_if_modern_and_true (GMT, THIS_MODULE_NEEDS, Ctrl->G.active || Ctrl->Q.active);

	n_errors += gmt_check_binary_io (GMT, 2);
	n_errors += gmt_M_check_condition (GMT, GMT->common.R.active[ISET] && (GMT->common.R.inc[GMT_X] <= 0.0 || GMT->common.R.inc[GMT_Y] <= 0.0), "Syntax error -I option: Must specify positive increment(s)\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->G.active && !Ctrl->G.file, "Syntax error -G option: Must specify file name\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->G.active && (GMT->common.R.active[ISET] + GMT->common.R.active[RSET]) != 2, "Syntax error: Must specify -R, -I, -G for gridding\n");
	(void)gmt_M_check_condition (GMT, !Ctrl->G.active && GMT->common.R.active[ISET], "Warning: -I not needed when -G is not set\n");
	(void)gmt_M_check_condition (GMT, !(Ctrl->G.active || Ctrl->Q.active) && GMT->common.R.active[RSET], "Warning: -R not needed when -G or -Q are not set\n");
	//n_errors += gmt_M_check_condition (GMT, Ctrl->F.active && !Ctrl->G.active, "Syntax error -F option: Cannot be used without -G\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->S.active && Ctrl->Q.active, "Syntax error -S option: Cannot be used with -Q\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->N.active && !Ctrl->G.active, "Syntax error -N option: Only required with -G\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->Q.active && !GMT->common.R.active[RSET], "Syntax error -Q option: Requires -R\n");
	n_errors += gmt_M_check_condition (GMT, Ctrl->Q.active && GMT->current.setting.triangulate == GMT_TRIANGLE_WATSON, "Syntax error -Q option: Requires Shewchuk triangulation algorithm\n");
	if (!(Ctrl->M.active || Ctrl->Q.active || Ctrl->S.active || Ctrl->N.active)) Ctrl->N.active = !Ctrl->G.active;	/* The default action */

	return (n_errors ? GMT_PARSE_ERROR : GMT_NOERROR);
}

#define bailout(code) {gmt_M_free_options (mode); return (code);}
#define Return(code) {Free_Ctrl (GMT, Ctrl); gmt_end_module (GMT, GMT_cpy); bailout (code);}

int GMT_triangulate (void *V_API, int mode, void *args) {
	int *link = NULL;	/* Must remain int and not int due to triangle function */
	
	uint64_t ij, ij1, ij2, ij3, np, i, j, k, n_edge, p, node = 0, seg, n = 0;
	unsigned int n_input, n_output, side;
	int row, col, col_min, col_max, row_min, row_max, error = 0;
	bool triplets[2] = {false, false}, map_them = false, do_output = true, get_input = false;
	
	size_t n_alloc;
	
	double zj, zk, zl, zlj, zkj, xp, yp, a, b, c, f;
	double xkj, xlj, ykj, ylj, out[3], vx[4], vy[4];
	double *xx = NULL, *yy = NULL, *zz = NULL, *in = NULL, *zpol = NULL;
	double *xf = NULL, *yf = NULL;

	char *tri_algorithm[2] = {"Watson", "Shewchuk"};
	char record[GMT_BUFSIZ];

	struct GMT_GRID *Grid = NULL, *F = NULL;
	struct GMT_DATASET *V = NULL;
	struct GMT_DATASEGMENT *P = NULL;

	struct TRIANGULATE_EDGE *edge = NULL;
	struct TRIANGULATE_CTRL *Ctrl = NULL;
	struct GMT_CTRL *GMT = NULL, *GMT_cpy = NULL;
	struct GMT_OPTION *options = NULL;
	struct GMTAPI_CTRL *API = gmt_get_api_ptr (V_API);	/* Cast from void to GMTAPI_CTRL pointer */

	/*----------------------- Standard module initialization and parsing ----------------------*/

	if (API == NULL) return (GMT_NOT_A_SESSION);
	if (mode == GMT_MODULE_PURPOSE) return (usage (API, GMT_MODULE_PURPOSE));	/* Return the purpose of program */
	options = GMT_Create_Options (API, mode, args);	if (API->error) return (API->error);	/* Set or get option list */

	if (!options || options->option == GMT_OPT_USAGE) bailout (usage (API, GMT_USAGE));/* Return the usage message */
	if (options->option == GMT_OPT_SYNOPSIS) bailout (usage (API, GMT_SYNOPSIS));	/* Return the synopsis */

	/* Parse the command-line arguments */

	if ((GMT = gmt_init_module (API, THIS_MODULE_LIB, THIS_MODULE_NAME, THIS_MODULE_KEYS, THIS_MODULE_NEEDS, &options, &GMT_cpy)) == NULL) bailout (API->error); /* Save current state */
	if (GMT_Parse_Common (API, THIS_MODULE_OPTIONS, options)) Return (API->error);
	Ctrl = New_Ctrl (GMT);	/* Allocate and initialize a new control structure */
	if ((error = parse (GMT, Ctrl, options)) != 0) Return (error);

	/*---------------------------- This is the triangulate main code ----------------------------*/

	GMT_Report (API, GMT_MSG_LONG_VERBOSE, "%s triangulation algorithm selected\n", tri_algorithm[GMT->current.setting.triangulate]);
	get_input = (!(Ctrl->F.active && Ctrl->F.mode == 0));
	
	GMT_Report (API, GMT_MSG_VERBOSE, "Processing input table data\n");
	
	if (Ctrl->G.active) {
		if ((Grid = GMT_Create_Data (API, GMT_IS_GRID, GMT_IS_SURFACE, GMT_CONTAINER_ONLY, NULL, NULL, NULL, \
			GMT_GRID_DEFAULT_REG, GMT_NOTSET, NULL)) == NULL) Return (API->error);
	}
	if (Ctrl->Q.active && Ctrl->Z.active) GMT_Report (API, GMT_MSG_LONG_VERBOSE, "Warning: We will read (x,y,z), but only (x,y) will be output when -Q is used\n");
	n_output = (Ctrl->N.active) ? 3 : 2;
	if (Ctrl->M.active && Ctrl->Z.active) n_output = 3;
	triplets[GMT_OUT] = (n_output == 3);
	if (Ctrl->G.active && !Ctrl->T.active) do_output = false;	/* If gridding then we require -T to also output the spatial files */
	if ((error = gmt_set_cols (GMT, GMT_OUT, n_output)) != 0) Return (error);
	n_input = (Ctrl->G.active || Ctrl->Z.active) ? 3 : 2;
	triplets[GMT_IN] = (n_input == 3);
	if (n_output > n_input) triplets[GMT_OUT] = false;	/* No can do. */
	
	if (Ctrl->G.active && GMT->common.R.active[RSET] && GMT->common.J.active) { /* Gave -R -J */
		map_them = true;
		if (gmt_M_err_pass (GMT, gmt_map_setup (GMT, Grid->header->wesn), "")) Return (GMT_PROJECTION_ERROR);
	}

	/* Now we are ready to take on some input values */

	n_alloc = GMT_INITIAL_MEM_ROW_ALLOC;
	xx = gmt_M_memory (GMT, NULL, n_alloc, double);
	yy = gmt_M_memory (GMT, NULL, n_alloc, double);
	if (triplets[GMT_IN]) zz = gmt_M_memory (GMT, NULL, n_alloc, double);
	n = 0;

	if (get_input) {	/* Read primary input */
		if ((error = gmt_set_cols (GMT, GMT_IN, n_input)) != GMT_NOERROR) {
			Return (error);
		}
	}

	/* Initialize the i/o since we are doing record-by-record reading/writing */
	if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_IN, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR) {	/* Establishes data input */
		if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
		gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
		Return (API->error);
	}
	if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_IN, GMT_HEADER_ON) != GMT_NOERROR) {	/* Enables data input and sets access mode */
		if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
		gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
		Return (API->error);
	}

	do {	/* Keep returning records until we reach EOF */
		if ((in = GMT_Get_Record (API, GMT_READ_DATA, NULL)) == NULL) {	/* Read next record, get NULL if special case */
			if (gmt_M_rec_is_error (GMT)) {		/* Bail if there are any read errors */
				if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
				gmt_M_free (GMT, xx);		gmt_M_free (GMT, yy);
				Return (GMT_RUNTIME_ERROR);
			}
			else if (gmt_M_rec_is_eof (GMT)) 		/* Reached end of file */
				break;
			continue;	/* Go back and read the next record */
		}

		/* Data record to process */

		xx[n] = in[GMT_X];	yy[n] = in[GMT_Y];
		if (triplets[GMT_IN]) zz[n] = in[GMT_Z];
		n++;

		if (n == n_alloc) {	/* Get more memory */
			n_alloc <<= 1;
			xx = gmt_M_memory (GMT, xx, n_alloc, double);
			yy = gmt_M_memory (GMT, yy, n_alloc, double);
			if (triplets[GMT_IN]) zz = gmt_M_memory (GMT, zz, n_alloc, double);
		}
	} while (true);
	
	if (GMT_End_IO (API, GMT_IN, 0) != GMT_NOERROR) {	/* Disables further data input */
		if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
		gmt_M_free (GMT, xx);		gmt_M_free (GMT, yy);
		Return (API->error);
	}

	if (Ctrl->F.active) {	/* Use non-NaN nodes in a previous grid as input data, possibly in addition to input records */
		double *wesn = (Ctrl->G.active) ? Grid->header->wesn : NULL;
		double xnoise, ynoise, rx, ry, percent = 5.0, fraction = 0.01 * percent;
		if ((F = GMT_Read_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_CONTAINER_AND_DATA, wesn, Ctrl->F.file, NULL)) == NULL) {
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			gmt_M_free (GMT, xx);		gmt_M_free (GMT, yy);
			Return (API->error);	/* Get subset if exceeding desired grid */
		}
		xf = gmt_grd_coord (GMT, F->header, GMT_X);
		yf = gmt_grd_coord (GMT, F->header, GMT_Y);
		xnoise = fraction * F->header->inc[GMT_X];
		ynoise = fraction * F->header->inc[GMT_Y];
		gmt_M_grd_loop (GMT, F, row, col, ij) {
			if (gmt_M_is_fnan (F->data[ij]))	continue;	/* Only add real data to the input list */
			rx = xnoise * gmt_nrand (GMT);
			ry = ynoise * gmt_nrand (GMT);
			xx[n] = xf[col] + rx;	yy[n] = yf[row] + ry;
			GMT_Report (API, GMT_MSG_DEBUG, "Adding grid point with %g noise: %d\t	%g\t%g\n", percent, (int)n, xx[n], yy[n]);
			if (triplets[GMT_IN]) zz[n] = F->data[ij];
			n++;
			if (n == n_alloc) {	/* Get more memory */
				n_alloc <<= 1;
				xx = gmt_M_memory (GMT, xx, n_alloc, double);
				yy = gmt_M_memory (GMT, yy, n_alloc, double);
				zz = gmt_M_memory (GMT, zz, n_alloc, double);
			}
		}
	}
	
	if (n >= INT_MAX) {
		GMT_Report (API, GMT_MSG_NORMAL, "Error: Cannot triangulate more than %d points\n", INT_MAX);
		gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
		if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
		Return (GMT_RUNTIME_ERROR);
	}
	
	xx = gmt_M_memory (GMT, xx, n, double);
	yy = gmt_M_memory (GMT, yy, n, double);
	if (triplets[GMT_IN]) zz = gmt_M_memory (GMT, zz, n, double);

	if (n == 0) {
		GMT_Report (API, GMT_MSG_NORMAL, "Error: No data points given - so no triangulation can take effect\n");
		gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
		if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
		Return (GMT_RUNTIME_ERROR);
	}

	if (map_them) {	/* Must make parallel arrays for projected x/y */
		double *xxp = NULL, *yyp = NULL;

		xxp = gmt_M_memory (GMT, NULL, n, double);
		yyp = gmt_M_memory (GMT, NULL, n, double);
		for (i = 0; i < n; i++) gmt_geo_to_xy (GMT, xx[i], yy[i], &xxp[i], &yyp[i]);

		GMT_Report (API, GMT_MSG_VERBOSE, "Do Delaunay optimal triangulation on projected coordinates\n");

		if (Ctrl->Q.active)
			V = gmt_voronoi (GMT, xxp, yyp, n, GMT->current.proj.rect, Ctrl->Q.mode);
		else
			np = gmt_delaunay (GMT, xxp, yyp, n, &link);

		gmt_M_free (GMT, xxp);	/* Cannot do this if we are doing NN gridding since we need xxp, yyp */
		gmt_M_free (GMT, yyp);
	}
	else {
		GMT_Report (API, GMT_MSG_VERBOSE, "Do Delaunay optimal triangulation on given coordinates\n");

		if (Ctrl->Q.active)
			V = gmt_voronoi (GMT, xx, yy, n, GMT->common.R.wesn, Ctrl->Q.mode);
		else
			np = gmt_delaunay (GMT, xx, yy, n, &link);
	}

	if (Ctrl->Q.active) {
		char header[GMT_LEN64] = {""};
		char *feature[2] = {"edges", "polygons"};
		GMT_Report (API, GMT_MSG_VERBOSE, "%" PRIu64 " Voronoi %s found\n", V->n_segments, feature[Ctrl->Q.mode]);
		zpol = gmt_M_memory (GMT, NULL, V->n_segments, double);
		if (triplets[GMT_IN] && Ctrl->Q.mode) {
			for (seg = 0; seg < V->n_segments; seg++) {
				P = V->table[0]->segment[seg];
				/* Annoyingly, must first identify the input data point that is inside this polygon */
				for (k = side = 0; !side && k < n; k++) {
					if (yy[k] < P->min[GMT_Y] || yy[k] > P->max[GMT_Y]) continue;
					if (xx[k] < P->min[GMT_X] || xx[k] > P->max[GMT_X]) continue;
					side = gmt_inonout (GMT, xx[k], yy[k], P);
					if (side) node = k;	/* Found the data node */
				}
				zpol[seg] = zz[node];
				sprintf (header, "%s -Z%g", P->header, zpol[seg]);
				gmt_M_str_free (P->header);
				P->header = strdup (header);
			}
		}
	}
	else
		GMT_Report (API, GMT_MSG_VERBOSE, "%" PRIu64 " Delaunay triangles found\n", np);
	
	if (Ctrl->G.active) {	/* Need to set up an output grid  */
		if (Ctrl->F.active && gmt_M_grd_same_shape (GMT, Grid, F) && gmt_M_grd_same_region (GMT, Grid, F) && !gmt_M_file_is_memory (Ctrl->F.file)) {
			/* F and G are the same region and F is not a memory grid via API.  Reuse F->data for G */
			Grid->data = F->data;	F->data = NULL;
		}
		else if (GMT_Create_Data (API, GMT_IS_GRID, GMT_IS_GRID, GMT_DATA_ONLY, NULL, NULL, NULL, 0, 0, Grid) == NULL) {
			if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			Return (API->error);
		}
		if (Ctrl->F.active && F->data) {	/* Not the same area, must copy over pregrid node values */
			unsigned int frow, fcol;
			gmt_M_grd_loop (GMT, F, frow, fcol, ij) {
				row = gmt_M_grd_y_to_row (GMT, yf[frow], Grid->header);
				if (row < 0 || row >= (int)Grid->header->n_columns) continue;
				col = gmt_M_grd_x_to_col (GMT, xf[col], Grid->header);
				if (col < 0 || col >= (int)Grid->header->n_rows) continue;
				p = gmt_M_ijp (Grid->header, row, col);
				Grid->data[p] = F->data[ij];	/* This also copies the NaNs from F to Grid */
			}
		}
	}
	if (Ctrl->F.active) {	/* No longer need these */
		gmt_M_free (GMT, xf);
		gmt_M_free (GMT, yf);
	}
	
	if (Ctrl->G.active && Ctrl->Q.active) {	/* Grid via natural nearest neighbor using Voronoi polygons */
		bool periodic, duplicate_col;
		int s_row, south_row, north_row, w_col, e_col;
		unsigned int row, col, p_col, west_col, east_col, nx1;
		uint64_t n_set = 0;
		double *grid_lon = NULL, *grid_lat = NULL;
		GMT_Report (API, GMT_MSG_VERBOSE, "Perform natural nearest neighbor gridding\n");
		
		nx1 = (Grid->header->registration == GMT_GRID_PIXEL_REG) ? Grid->header->n_columns : Grid->header->n_columns - 1;
		periodic = gmt_M_360_range (GMT->common.R.wesn[XLO], GMT->common.R.wesn[XHI]);
		duplicate_col = (periodic && Grid->header->registration == GMT_GRID_NODE_REG);	/* E.g., lon = 0 column should match lon = 360 column */
		grid_lon = gmt_grd_coord (GMT, Grid->header, GMT_X);
		grid_lat = gmt_grd_coord (GMT, Grid->header, GMT_Y);

		for (seg = 0; seg < V->n_segments; seg++) {
			P = V->table[0]->segment[seg];
			/* Find bounding row/cols over which to loop for this polygon */
			south_row = (int)gmt_M_grd_y_to_row (GMT, P->min[GMT_Y], Grid->header);
			north_row = (int)gmt_M_grd_y_to_row (GMT, P->max[GMT_Y], Grid->header);
			w_col  = (int)gmt_M_grd_x_to_col (GMT, P->min[GMT_X], Grid->header);
			while (w_col < 0) w_col += nx1;
			west_col = w_col;
			e_col = (int)gmt_M_grd_x_to_col (GMT, P->max[GMT_X], Grid->header);
			while (e_col < w_col) e_col += nx1;
			east_col = e_col;
			/* So here, any polygon will have a positive (or 0) west_col with an east_col >= west_col */
			for (s_row = north_row; s_row <= south_row; s_row++) {	/* For each scanline intersecting this polygon */
				if (s_row < 0) continue;	/* North of region */
				row = s_row; if (row >= Grid->header->n_rows) continue;	/* South of region */
				for (p_col = west_col; p_col <= east_col; p_col++) {	/* March along the scanline using col >= 0 */
					if (p_col >= Grid->header->n_columns) {	/* Off the east end of the grid */
						if (periodic)	/* Just shuffle to the corresponding point inside the global grid */
							col = p_col - nx1;
						else		/* Sorry, really outside the region */
							continue;
					}
					else
						col = p_col;
					if (Ctrl->F.active) {	/* Only do interpolation at this point if grid == NaN so check before doing slow gmt_inonout */
						p = gmt_M_ijp (Grid->header, row, col);
						if (!gmt_M_is_fnan (Grid->data[p])) continue;
					}
					side = gmt_inonout (GMT, grid_lon[col], grid_lat[row], P);
					if (side == 0) continue;	/* Outside polygon */
					p = gmt_M_ijp (Grid->header, row, col);
					Grid->data[p] = (float)zpol[seg];
					n_set++;
					if (duplicate_col) {	/* Duplicate the repeating column on the other side of this one */
						if (col == 0) Grid->data[p+nx1] = Grid->data[p], n_set++;
						else if (col == nx1) Grid->data[p-nx1] = Grid->data[p], n_set++;
					}
				}
			}
		}
		gmt_M_free (GMT, grid_lon);	gmt_M_free (GMT, grid_lat);
		if (GMT_Set_Comment (API, GMT_IS_GRID, GMT_COMMENT_IS_OPTION | GMT_COMMENT_IS_COMMAND, options, Grid)) {
			if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			Return (API->error);
		}
		if (GMT_Write_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_CONTAINER_AND_DATA, NULL, Ctrl->G.file, Grid) != GMT_NOERROR) {
			if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			Return (API->error);
		}
		GMT_Report (API, GMT_MSG_VERBOSE, "Done!\n");
	}
	else if (Ctrl->G.active && !Ctrl->Q.active) {	/* Grid via planar triangle segments */
		int n_columns = Grid->header->n_columns, n_rows = Grid->header->n_rows;	/* Signed versions */
		GMT_Report (API, GMT_MSG_VERBOSE, "Perform Delaunay triangle gridding\n");
		if (!Ctrl->F.active) {
			if (!Ctrl->E.active) Ctrl->E.value = GMT->session.d_NaN;
			for (p = 0; p < Grid->header->size; p++) Grid->data[p] = (float)Ctrl->E.value;	/* initialize grid */
		}

		for (k = ij = 0; k < np; k++) {

			/* Find equation for the plane as z = ax + by + c */

			vx[0] = vx[3] = xx[link[ij]];	vy[0] = vy[3] = yy[link[ij]];	zj = zz[link[ij++]];
			vx[1] = xx[link[ij]];	vy[1] = yy[link[ij]];	zk = zz[link[ij++]];
			vx[2] = xx[link[ij]];	vy[2] = yy[link[ij]];	zl = zz[link[ij++]];

			xkj = vx[1] - vx[0];	ykj = vy[1] - vy[0];
			zkj = zk - zj;	xlj = vx[2] - vx[0];
			ylj = vy[2] - vy[0];	zlj = zl - zj;

			f = 1.0 / (xkj * ylj - ykj * xlj);
			a = -f * (ykj * zlj - zkj * ylj);
			b = -f * (zkj * xlj - xkj * zlj);
			c = -a * vx[1] - b * vy[1] + zk;

			/* Compute grid indices the current triangle may cover, assuming all triangles are
			   in the -R region (Grid->header->wesn[XLO]/x_max etc.)  Always, col_min <= col_max, row_min <= row_max.
			 */

			xp = MIN (MIN (vx[0], vx[1]), vx[2]);	col_min = (int)gmt_M_grd_x_to_col (GMT, xp, Grid->header);
			xp = MAX (MAX (vx[0], vx[1]), vx[2]);	col_max = (int)gmt_M_grd_x_to_col (GMT, xp, Grid->header);
			yp = MAX (MAX (vy[0], vy[1]), vy[2]);	row_min = (int)gmt_M_grd_y_to_row (GMT, yp, Grid->header);
			yp = MIN (MIN (vy[0], vy[1]), vy[2]);	row_max = (int)gmt_M_grd_y_to_row (GMT, yp, Grid->header);

			/* Adjustments for triangles outside -R region. */
			/* Triangle to the left or right. */
			if ((col_max < 0) || (col_min >= n_columns)) continue;
			/* Triangle Above or below */
			if ((row_max < 0) || (row_min >= n_rows)) continue;

			/* Triangle covers boundary, left or right. */
			if (col_min < 0) col_min = 0;       if (col_max >= n_columns) col_max = Grid->header->n_columns - 1;
			/* Triangle covers boundary, top or bottom. */
			if (row_min < 0) row_min = 0;       if (row_max >= n_rows) row_max = Grid->header->n_rows - 1;

			for (row = row_min; row <= row_max; row++) {
				yp = gmt_M_grd_row_to_y (GMT, row, Grid->header);
				p = gmt_M_ijp (Grid->header, row, col_min);
				for (col = col_min; col <= col_max; col++, p++) {
					if (Ctrl->F.active && !gmt_M_is_fnan (Grid->data[p])) continue;	/* Only do interpolation at this point if grid == NaN so check before doing gmt_non_zero_winding */

					xp = gmt_M_grd_col_to_x (GMT, col, Grid->header);
					if (!gmt_non_zero_winding (GMT, xp, yp, vx, vy, 4)) continue;	/* Outside */

					if (Ctrl->D.dir == GMT_X)
						Grid->data[p] = (float)a;
					else if (Ctrl->D.dir == GMT_Y)
						Grid->data[p] = (float)b;
					else
						Grid->data[p] = (float)(a * xp + b * yp + c);
				}
			}
		}
		if (GMT_Set_Comment (API, GMT_IS_GRID, GMT_COMMENT_IS_OPTION | GMT_COMMENT_IS_COMMAND, options, Grid)) {
			if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			Return (API->error);
		}
		if (GMT_Write_Data (API, GMT_IS_GRID, GMT_IS_FILE, GMT_IS_SURFACE, GMT_CONTAINER_AND_DATA, NULL, Ctrl->G.file, Grid) != GMT_NOERROR) {
			if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			Return (API->error);
		}
		GMT_Report (API, GMT_MSG_VERBOSE, "Done!\n");
	}
	
	if (do_output && (Ctrl->M.active || Ctrl->Q.active || Ctrl->S.active || Ctrl->N.active)) {	/* Requires output to stdout */
		if (!Ctrl->Q.active) {	/* Still record-by-record output */
			if (GMT_Init_IO (API, GMT_IS_DATASET, GMT_IS_POINT, GMT_OUT, GMT_ADD_DEFAULT, 0, options) != GMT_NOERROR) {	/* Establishes data output */
				if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
				if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
				gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
				Return (API->error);
			}
			if (GMT_Begin_IO (API, GMT_IS_DATASET, GMT_OUT, GMT_HEADER_ON) != GMT_NOERROR) {	/* Enables data output and sets access mode */
				if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
				if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
				gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
				Return (API->error);
			}
		}
		if (Ctrl->M.active || Ctrl->Q.active) {	/* Must find unique edges to output only once */
			gmt_set_segmentheader (GMT, GMT_OUT, true);
			if (Ctrl->Q.active) {	/* Voronoi edges */
				if (GMT_Write_Data (API, GMT_IS_DATASET, GMT_IS_FILE, V->geometry, V->io_mode, NULL, Ctrl->Out.file, V) != GMT_NOERROR) {
					gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
					if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
					Return (API->error);
				}
			}
			else {	/* Triangle edges */
				if (GMT_Set_Geometry (API, GMT_OUT, GMT_IS_LINE) != GMT_NOERROR) {	/* Sets output geometry */
					if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
					gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
					if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
					Return (API->error);
				}
				n_edge = 3 * np;
				edge = gmt_M_memory (GMT, NULL, n_edge, struct TRIANGULATE_EDGE);
				for (i = ij1 = 0, ij2 = 1, ij3 = 2; i < np; i++, ij1 += 3, ij2 += 3, ij3 += 3) {
					edge[ij1].begin = link[ij1];	edge[ij1].end = link[ij2];
					edge[ij2].begin = link[ij2];	edge[ij2].end = link[ij3];
					edge[ij3].begin = link[ij1];	edge[ij3].end = link[ij3];
				}
				for (i = 0; i < n_edge; i++) if (edge[i].begin > edge[i].end) gmt_M_int_swap (edge[i].begin, edge[i].end);

				qsort (edge, n_edge, sizeof (struct TRIANGULATE_EDGE), compare_edge);
				for (i = 1, j = 0; i < n_edge; i++) {
					if (edge[i].begin != edge[j].begin || edge[i].end != edge[j].end) j++;
					edge[j] = edge[i];
				}
				n_edge = j + 1;

				GMT_Report (API, GMT_MSG_VERBOSE, "%" PRIu64 " unique triangle edges\n", n_edge);

				for (i = 0; i < n_edge; i++) {
					sprintf (record, "Edge %d-%d", edge[i].begin, edge[i].end);
					GMT_Put_Record (API, GMT_WRITE_SEGMENT_HEADER, record);
					out[GMT_X] = xx[edge[i].begin];	out[GMT_Y] = yy[edge[i].begin];	if (triplets[GMT_OUT]) out[GMT_Z] = zz[edge[i].begin];
					GMT_Put_Record (API, GMT_WRITE_DATA, out);
					out[GMT_X] = xx[edge[i].end];	out[GMT_Y] = yy[edge[i].end];	if (triplets[GMT_OUT]) out[GMT_Z] = zz[edge[i].end];
					GMT_Put_Record (API, GMT_WRITE_DATA, out);
				}
				gmt_M_free (GMT, edge);
			}
		}
		else if (Ctrl->S.active)  {	/* Write triangle polygons */
			if (GMT_Set_Geometry (API, GMT_OUT, GMT_IS_POLY) != GMT_NOERROR) {	/* Sets output geometry */
				if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);	/* Coverity says it would leak */
				gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
				if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
				gmt_M_free (GMT, xx);
				gmt_M_free (GMT, yy);
				Return (API->error);
			}
			gmt_set_segmentheader (GMT, GMT_OUT, true);
			for (i = ij = 0; i < np; i++, ij += 3) {
				sprintf (record, "Polygon %d-%d-%d -Z%" PRIu64, link[ij], link[ij+1], link[ij+2], i);
				GMT_Put_Record (API, GMT_WRITE_SEGMENT_HEADER, record);
				for (k = 0; k < 3; k++) {	/* Three vertices */
					out[GMT_X] = xx[link[ij+k]];	out[GMT_Y] = yy[link[ij+k]];	if (triplets[GMT_OUT]) out[GMT_Z] = zz[link[ij+k]];
					GMT_Put_Record (API, GMT_WRITE_DATA, out);	/* Write this to output */
				}
				/* Explicitly close the polygon */
				out[GMT_X] = xx[link[ij]];	out[GMT_Y] = yy[link[ij]];	if (triplets[GMT_OUT]) out[GMT_Z] = zz[link[ij]];
				GMT_Put_Record (API, GMT_WRITE_DOUBLE, out);	/* Write this to output */
			}
		}
		else if (Ctrl->N.active) {	/* Write table of indices */
			/* Set output format to regular float */
			gmt_set_cartesian (GMT, GMT_OUT);	/* Since output is no longer lon/lat */
			GMT->current.io.col_type[GMT_OUT][GMT_Z] = GMT_IS_FLOAT;
			for (i = ij = 0; i < np; i++, ij += 3) {
				for (k = 0; k < 3; k++) out[k] = (double)link[ij+k];
				GMT_Put_Record (API, GMT_WRITE_DATA, out);	/* Write this to output */
			}
		}
		if (!Ctrl->Q.active && GMT_End_IO (API, GMT_OUT, 0) != GMT_NOERROR) {	/* Disables further data output */
			gmt_M_free (GMT, xx);	gmt_M_free (GMT, yy);
			if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
			gmt_M_free (GMT, xx);
			gmt_M_free (GMT, yy);
			Return (API->error);
		}
	}

	gmt_M_free (GMT, xx);
	gmt_M_free (GMT, yy);
	if (zpol) gmt_M_free (GMT, zpol);
	if (triplets[GMT_IN]) gmt_M_free (GMT, zz);
	if (!Ctrl->Q.active) gmt_delaunay_free (GMT, &link);
	GMT_Report (API, GMT_MSG_VERBOSE, "Done!\n");

	Return (GMT_NOERROR);
}