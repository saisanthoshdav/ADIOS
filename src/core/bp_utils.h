/* 
 * ADIOS is freely available under the terms of the BSD license described
 * in the COPYING file in the top level directory of this source distribution.
 *
 * Copyright (c) 2008 - 2009.  UT-BATTELLE, LLC. All rights reserved.
 */

#ifndef __BP_UTILS_H__
#define __BP_UTILS_H__

#include <stdio.h>
#include <sys/types.h>
#   include "public/adios_read.h" // ADIOS_FILE*
#ifdef _NOMPI
#   include "public/mpidummy.h"
#else
#   include "mpi.h"
#endif
#include "core/bp_types.h"
#define VARS_MINIHEADER_SIZE 10

void bp_alloc_aligned (struct adios_bp_buffer_struct_v1 * b, uint64_t size);
void bp_realloc_aligned (struct adios_bp_buffer_struct_v1 * b, uint64_t size);
int bp_parse_characteristics (struct adios_bp_buffer_struct_v1 * b,
		  	      struct adios_index_var_struct_v1 ** root,
			      uint64_t j);
int bp_get_characteristics_data (void ** ptr_data,
				 void * buffer,
				 int data_size,
				 enum ADIOS_DATATYPES type);
int bp_read_close (struct adios_bp_buffer_struct_v1 * b);
int bp_read_open (const char * filename,
	 	  MPI_Comm comm, 
		  struct BP_FILE * fh);
int bp_open (const char * fname,
             MPI_Comm comm,
             struct BP_FILE * fh);
int bp_read_minifooter (struct BP_FILE * bp_struct);
int bp_parse_pgs (struct BP_FILE * fh);
int bp_parse_attrs (struct BP_FILE * fh);
int bp_parse_vars (struct BP_FILE * fh);
int bp_seek_to_step (ADIOS_FILE * fp, int tostep);

const char * bp_value_to_string (enum ADIOS_DATATYPES type, void * data);
int bp_get_type_size (enum ADIOS_DATATYPES type, void * var);
int bp_get_dimensioncharacteristics(struct adios_index_characteristic_struct_v1 *ch,
                                    uint64_t *ldims, uint64_t *gdims, uint64_t *offsets);
int bp_get_dimension_characteristics_notime (struct adios_index_characteristic_struct_v1 *ch,
                                            uint64_t *ldims, uint64_t *gdims, uint64_t *offsets,
                                            int file_is_fortran);
void bp_get_dimensions (struct adios_index_var_struct_v1 *var_root, int file_is_fortran,
                        int *ndim, uint64_t **dims);
void bp_get_and_swap_dimensions (struct adios_index_var_struct_v1 *var_root, int file_is_fortran,
                                 int *ndim, uint64_t **dims, int swap_flag);
int get_var_nsteps (struct adios_index_var_struct_v1 * var_root);
int * get_var_nblocks (struct adios_index_var_struct_v1 * var_root, int nsteps);
void print_process_group_index (
                         struct adios_index_process_group_struct_v1 * pg_root
                         );
/* Return 1 if a < b wrt. the given type, otherwise 0 */
int adios_lt(int type, void *a, void *b);
double bp_value_to_double(enum ADIOS_DATATYPES type, void * data);
int is_fortran_file (struct BP_FILE * fh);
int has_subfiles (struct BP_FILE * fh);
struct adios_index_var_struct_v1 * bp_find_var_byid (struct BP_FILE * fh, int varid);
int is_global_array (struct adios_index_characteristic_struct_v1 * ch);
int check_bp_validity (const char * fname, MPI_Comm comm);
#endif