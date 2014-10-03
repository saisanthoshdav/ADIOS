#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "public/adios_read.h"
#include <iapi.h>

#include "fastbit_adios.h"

  char gVarNameFastbitIdxKey[10] = "key";
  char gVarNameFastbitIdxOffset[10] = "offsets";
  char gVarNameFastbitIdxBms[10] = "bms";

  char gGroupNameFastbitIdx[20] = "notNamed";
  char gEmptyStr[] = ""; // local string & offset string

  int64_t       gAdios_group;
  int64_t       gAdios_write_file;


void printData(void* data, enum ADIOS_DATATYPES type, uint64_t size);

void defineFastbitVar(int nblocks, const char* name, int64_t* ids, int adiosType, uint64_t* localDim, const char* globalStr, uint64_t* offset)
{
  int i=0;
  for (i = 0; i < nblocks; i++) 
  {
    //int offset= i*5;
    char offsetStr[100] = "";
    if (offset != NULL) {
      sprintf(offsetStr, "%ld", offset[i]);
    } 

    char dimStr[100];
    sprintf(dimStr, "%ld", localDim[i]);
    ids[i] = adios_define_var (gAdios_group, name, "", adiosType, dimStr, globalStr, offsetStr);
    adios_set_transform (ids[i], "identity");
  }
}


// k is numbered from 1 to sum_nblocks
void verifyData(ADIOS_FILE* f, ADIOS_VARINFO* v, int k, int timestep) 
{
  uint64_t blockBytes = adios_type_size (v->type, v->value);
  int j=0;

  if (v->ndim <= 0) {
    return;
  }

  printf("verify block[%d]: ", k);
  
      for (j=0; j<v->ndim; j++) 
      {  
	  blockBytes *= v->blockinfo[k].count[j];
	  printf("%llu:%llu ", v->blockinfo[k].start[j], v->blockinfo[k].count[j]);
      }

      void* data = NULL;
      data = malloc(blockBytes);  
      ADIOS_SELECTION* sel =  adios_selection_boundingbox (v->ndim, v->blockinfo[k].start, v->blockinfo[k].count);
      int err = adios_schedule_read_byid(f, sel, v->varid, timestep, 1, data);      
      if (!err) {	
	   err = adios_perform_reads(f, 1);
      }
      printData(data, v->type, blockBytes/adios_type_size(v->type, v->value));
      adios_selection_delete(sel);
      free(data);	 
      data = NULL;
}



void assertErr(long int errCode, const char* exp, const char* refName) 
{
  if (errCode < 0) {
    printf("errCode=%d %s %s\n", errCode, exp, refName);
  } 
}

void fastbitIndex(const char* datasetName, void* data, uint64_t blockSize, FastBitDataType ft, 
		  double**keys, uint64_t*nk, int64_t**offsets, uint64_t*no,		  
		  uint32_t**bms, uint64_t*nb)
{
  long int fastbitErr;

  fastbitErr = fastbit_iapi_register_array(datasetName, ft, data, blockSize);
  assertErr(fastbitErr, "failed to register array with", datasetName);
  
  fastbitErr = fastbit_iapi_build_index(datasetName, (const char*)0);
  assertErr(fastbitErr, "failed to build idx with ", datasetName);
  
  
  fastbitErr = fastbit_iapi_deconstruct_index(datasetName, keys, nk, offsets, no, bms, nb);
  assertErr(fastbitErr, "failed with fastbit_iapi_deconstruct on ", datasetName);
  
  printf("nk/no/nb %lld %lld %lld\n", *nk, *no, *nb);
  
  fastbit_iapi_free_all();

  /*free(offsets);

  free(keys);

  free(bms);
  */
}

void buildIndex(ADIOS_FILE* f, ADIOS_VARINFO* v) 
{
      char bmsVarName[100];
      char keyVarName[100];
      char offsetName[100];

  adios_inq_var_blockinfo(f, v);

  int i=0;
  int j=0;
  int k=0;

  printf("  nsteps=%d, ndim=%d\n", v->nsteps, v->ndim);
  for (i=0; i<v->nsteps; i++) 
  {
      int nBlocksAtStep = v->nblocks[i];
      
      printf("\t\t   currstep=%d nblocks=%d\n", i, nBlocksAtStep);
  }


  for (k=0; k<v->sum_nblocks; k++) {
      uint64_t blockBytes = adios_type_size (v->type, v->value);
      uint64_t blockSize = 1;
      printf("  bytes calculator:  block %d: typeBytes=%llu", k, blockBytes);
      for (j=0; j<v->ndim; j++) 
      {  
	  blockBytes *= v->blockinfo[k].count[j];
	  blockSize *= v->blockinfo[k].count[j];
      }
    
      printf("\t\t   block %d, bytes: %llu, size = %llu \n", k, blockBytes, blockSize);      
      /*
      // read out the data in the block
      
      ADIOS_SELECTION* blockSel = adios_selection_writeblock(k);
      
      ...
      adios_schedule_read (f, blockSel, v->varid, timestep, 1, data);
      */
      // register to fastbit array
      // build index
      // serialize index
      // write out 
      // clean up in fastbit
      // clean up in adios
  }



  int blockCounter = -1;
  FastBitDataType ft = getFastbitDataType(v->type);

  for (i=0; i < v->nsteps; i++) {
       printf ("==> step %d: ",  i);

       int nblocks = v->nblocks[i];
       int64_t       var_ids_bms[nblocks];
       int64_t       var_ids_key[nblocks];
       int64_t       var_ids_offset[nblocks];

       for (j=0; j < v->nblocks[i]; j++) {
	 sprintf(bmsVarName, "bms-%d-%d-%d", v->varid, i, j);
	 sprintf(keyVarName, "key-%d-%d-%d", v->varid, i, j);
	 sprintf(offsetName, "offset-%d-%d-%d", v->varid, i, j);


	    blockCounter++;
	    uint64_t blockSize = getBlockSize(v, blockCounter);
	    uint64_t blockDataByteSize = adios_type_size (v->type, v->value) * blockSize; 
	    printf("   %d th block / (%d), size= %llu bytes=%llu", j, blockSize, blockCounter, blockDataByteSize);
	    
	    void* data = malloc (blockDataByteSize);
	    ADIOS_SELECTION* blockSel = adios_selection_writeblock(j);

	    //adios_selcton_writeblock(num),  0 <= num <  nblocks[timestep]
	    //ADIOS_SELECTION* blockSel = adios_selection_writeblock(blockCounter);
	    int err = adios_schedule_read_byid(f, blockSel, v->varid, i, 1, data);
	    if (!err) {	
	      err = adios_perform_reads(f, 1);
	    } else {
	      printf("Unable to read block %d at timestep: %d \n", j, i);
	      break;
	    }
	    printData(data, v->type, blockSize);
	   
	    double* keys = NULL;
	    int64_t *offsets = NULL;
	    uint32_t *bms = NULL;
	    uint64_t nk=0, no=0, nb=0;
	    
	    const char* datasetName = "test";
	    fastbitIndex(datasetName, data, blockSize, ft, &keys, &nk, &offsets, &no, &bms, &nb);

	    char nbStr[20], noStr[20], nkStr[20];
	    sprintf(nbStr, "%ld", nb); 
	    sprintf(nkStr, "%ld", nk); 
	    sprintf(noStr, "%ld", no); 

	    defineFastbitVar(1,bmsVarName, &var_ids_bms[j], adios_unsigned_integer, &nb,0,0);    			    
	    defineFastbitVar(1, keyVarName, &var_ids_key[j], adios_double, &nk, 0, 0);
	    defineFastbitVar(1, offsetName, &var_ids_offset[j], adios_long, &no, 0, 0); 

	    adios_write_byid(gAdios_write_file, var_ids_bms[j], bms);
	    adios_write_byid(gAdios_write_file, var_ids_key[j], keys);
	    adios_write_byid(gAdios_write_file, var_ids_offset[j], offsets);

	    adios_selection_delete(blockSel);
	    free(data);	 

	    verifyData(f, v, blockCounter, i);
       }

       fastbit_cleanup();

       printf("\n");
  }  
}

void buildIndexOnAllVar(ADIOS_FILE* f) 
{
  int numVars = f->nvars;
  
  int i=0;
  for (i=0; i<numVars; i++) {
    char* varName = f->var_namelist[i];
    ADIOS_VARINFO* v = adios_inq_var(f, varName);
    printf("\n... building fastbit index on  %dth variable: %s, ", i, varName);
    if (v->ndim > 0) {
      buildIndex(f, v);
    } else {
      printf("\t skipping scalar..\n");
    }
    adios_free_varinfo(v);
  }
}

int main (int argc, char** argv) 
{
  ADIOS_FILE * f;
  //MPI_Comm    comm_dummy = 0;  // MPI_Comm is defined through adios_read.h 
  MPI_Comm comm_dummy = MPI_COMM_WORLD;


  int         rank, size;
  MPI_Init (&argc, &argv);			   
  MPI_Comm_rank (comm_dummy, &rank);
  MPI_Comm_size (comm_dummy, &size);

  adios_init_noxml (comm_dummy);

  //char        idxFileNamePad [strlen(argv[1])];			   
  char        idxFileName    [strlen(argv[1])+20];			   
  //sprintf(idxFileName, "fastbitIdx%s", argv[1]); 
  
  //strncpy(idxFileNamePad, argv[1], strlen(argv[1])-3);
  //idxFileNamePad[strlen(argv[1])-3]=0;
  //sprintf(idxFileName, "%s-idx.bp", idxFileNamePad);
   
  f = adios_read_open_file (argv[1], ADIOS_READ_METHOD_BP, comm_dummy);
  if (f == NULL) {
    printf ("::%s\n", adios_errmsg());
    return -1;
  }

  adios_allocate_buffer (ADIOS_BUFFER_ALLOC_NOW, (f->file_size)*2/1048576);
  adios_declare_group (&gAdios_group, gGroupNameFastbitIdx, "", adios_flag_yes);
  adios_select_method (gAdios_group, "MPI", "", "");

  
  getIndexFileName(argv[1], idxFileName);
  //if (access(idxFileName, F_OK) == -1) { // does not exist
  unlink(idxFileName);
  adios_open (&gAdios_write_file, gGroupNameFastbitIdx, idxFileName, "w", comm_dummy);
  //} else {
  //adios_open (&gAdios_write_file, gGroupNameFastbitIdx, idxFileName, "a", comm_dummy);
    //}

  uint64_t adios_totalsize;
  adios_group_size (gAdios_write_file, 2*(f->file_size), &adios_totalsize);     

  printf("=> adios open output file: %s, totalsize allocated %llu bytes... \n", idxFileName, adios_totalsize); 

  printf("argc= %d\n", argc);
  if (argc >= 3) {
    int i=2;
    while (i<argc) {
      const char* varName = argv[i];
      ADIOS_VARINFO * v = adios_inq_var(f, varName);
      if (v == NULL) {
	printf("No such variable: %s\n", varName);
	return;
      }
      printf("building fastbit index on  variable: %s\n", varName);
      buildIndex(f, v);
      adios_free_varinfo(v);

      i++;
    }
  } else {
    buildIndexOnAllVar(f);
  }

  adios_close(gAdios_write_file);
  adios_read_close(f);

  //
  // writing file clean up
  //


  // read back:
  f = adios_read_open_file (idxFileName, ADIOS_READ_METHOD_BP, comm_dummy);
  if (f == NULL) {
    printf("No such file: %s \n", idxFileName);
    return 0;
  }

  int numVars = f->nvars;
  
  int i=0;
  int k=0;
  int j=0;
  for (i=0; i<numVars; i++) {
      char* varName = f->var_namelist[i];
      ADIOS_VARINFO* v = adios_inq_var(f, varName);

       adios_inq_var_blockinfo(f,v);      
      int timestep = 0;
      for (k=0; k<v->sum_nblocks; k++) {
	  verifyData(f, v, k, timestep);
      }

      adios_free_varinfo(v);
  }

  adios_read_close(f);



  // clean up
  MPI_Barrier (comm_dummy);
  adios_finalize (rank);
  MPI_Finalize ();

  return 0;
}


