#include "adios_types.h"

/* Asynchronous I/O API Public Functions */

/* Global setup and cleanup */
void adios_init (const char * config, MPI_Comm world, MPI_Comm self
                ,MPI_Info info_obj
                );
void adios_init_ (const char * config, MPI_Fint * world, MPI_Fint * self
                 ,MPI_Fint * info_obj, int config_size
                 );

void adios_finalize (int mype);
void adios_finalize_ (int * mype);

int adios_allocate_buffer (void);
int adios_allocate_buffer_ (void);

/* end user calls for each I/O operation */
void adios_get_group (long long * group_id, const char * name);
void adios_get_group_ (long long * group_id, const char * name, int name_size);

void adios_group_by (long long group_id, const char * group_by
                    ,MPI_Comm group_comm, int previous_id, int current_id
                    ,int next_id
                    );
void adios_group_by_ (long long * group_id, const char * group_by
                     ,int * group_comm, int * previous_id, int * current_id
                     ,int * next_id, int group_by_size
                     );

void adios_open (long long * fd, long long group, const char * name);
void adios_open_ (long long * fd, long long * group, const char * name
                 ,int name_size
                 );

void adios_open_append (long long * fd, long long group, const char * name);
void adios_open_append_ (long long * fd, long long * group, const char * name
                        ,int name_size
                        );

void adios_open_read (long long * fd, long long group, const char * name);
void adios_open_read_ (long long * fd, long long * group, const char * name
                      ,int name_size
                      );

void adios_set_offset (long long fd_p, long long offset);
void adios_set_offset_ (long long * fd_p, long long * offset);

void adios_write (long long fd_p, const char * name, void * var);
void adios_write_ (long long * fd_p, const char * name, void * var
                  ,int name_size
                  );

void adios_get_write_buffer (long long fd_p, const char * name
                            ,unsigned long long * size
                            ,void ** buffer
                            );
void adios_get_write_buffer_ (long long * fd_p, const char * name
                             ,unsigned long long * size
                             ,void ** buffer, int name_size
                             );

void adios_read (long long fd_p, const char * name, void * buffer);
void adios_read_ (long long * fd_p, const char * name, void * buffer
                 ,int name_size
                 );

void adios_set_path (long long group, const char * path);
void adios_set_path_ (long long * group, const char * path, int path_size);

void adios_set_path_var (long long group, const char * path, const char * name);
void adios_set_path_var_ (long long * group, const char * path
                         ,const char * name, int path_size, int name_size
                         );

void adios_get_data_size (long long fd_p, unsigned long long * size);
void adios_get_data_size_ (long long * fd_p, unsigned long long * size);

void adios_end_iteration (void);
void adios_end_iteration_ (void);

void adios_start_calculation (void);
void adios_start_calculation_ (void);

void adios_stop_calculation (void);
void adios_stop_calculation_ (void);

void adios_close (long long fd_p);
void adios_close_ (long long * fd_p);

/* Generally internal use called when parsing the XML file */
int adios_declare_group (long long * id, const char * name
                        ,const char * coordination_comm
                        ,const char * coordination_var
                        );
int adios_declare_group_ (long long * id, const char * name
                         ,const char * coordination_comm
                         ,const char * coordination_var
                         ,int name_size, int coordination_comm_size
                         ,int coordination_var_size
                         );

struct adios_global_bounds_struct;

int adios_define_var (long long group_id, const char * name
                     ,const char * path, int type
                     ,int copy_on_write
                     ,const char * dimensions
                     ,struct adios_global_bounds_struct * global_bounds
                     );
int adios_define_var_ (long long * group_id, const char * name
                      ,const char * path, int * type
                      ,int * copy_on_write
                      ,const char * dimensions
                      ,long long * global_bounds
                      ,int name_size, int path_size, int dimensions_size
                      );

int adios_define_global_bounds (long long group_id, const char * dimensions
                               ,const char * offsets
                               ,struct adios_global_bounds_struct ** global_bounds
                               );
int adios_define_global_bounds_ (long long * group_id, const char * dimensions
                                ,const char * offsets
                                ,long long * global_bounds
                                ,int dimensions_size, int offsets_size
                                );

int adios_define_attribute (long long group, const char * name
                           ,const char * path, const char * value
                           ,const char * type, const char * var
                           );
int adios_define_attribute_ (long long * group, const char * name
                            ,const char * path, const char * value
                            ,const char * type, const char * var
                            ,int name_size, int path_size, int value_size
                            ,int type_size, int var_size
                            );

int adios_select_method (int priority, const char * method
                        ,const char * parameters, const char * type
                        ,const char * base_path, int iters
                        );
int adios_select_method_ (int * priority, const char * method
                         ,const char * parameters, const char * type
                         ,const char * base_path, int * iters, int method_size
                         ,int parameters_size, int type_size, int base_path_size
                         );

int adios_select_method (int priority, const char * method
                        ,const char * parameters, const char * type
                        ,const char * base_path, int iters
                        );
int adios_select_method_ (int * priority, const char * method
                         ,const char * parameters, const char * type
                         ,const char * base_path, int * iters, int method_size
                         ,int parameters_size, int type_size, int base_path_size
                         );
