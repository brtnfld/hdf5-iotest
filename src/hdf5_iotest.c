/* hdf5-iotest -- simple I/O performance tester for HDF5

   SPDX-License-Identifier: BSD-3-Clause

   Copyright (C) 2020, The HDF Group

   hdf5-iotest is released under the New BSD license (see COPYING).
   Go to the project home page for more info:

   https://github.com/HDFGroup/hdf5-iotest

*/

#include "dataset.h"
#include "write_test.h"
#include "read_test.h"

#include "hdf5.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FILE "hdf5_iotest.ini"

herr_t set_libver_bounds(configuration* config, int rank, hid_t fapl);

int main(int argc, char* argv[])
{
  const char* ini = (argc > 1) ? argv[1] : CONFIG_FILE;

  configuration config;
  unsigned int strong_scaling_flg, coll_mpi_io_flg;

  int size, rank, my_proc_row, my_proc_col;
  unsigned long my_rows, my_cols;

  hid_t dxpl, fapl;
  hsize_t fsize;

  double wall_time, create_time, write_phase, write_time, read_phase, read_time;
  double min_create_time, max_create_time;
  double min_write_phase, max_write_phase;
  double min_write_time, max_write_time;
  double min_read_phase, max_read_phase;
  double min_read_time, max_read_time;

  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  wall_time = -MPI_Wtime();

  read_time = write_time = create_time = 0.0;

  if (rank == 0) /* rank 0 reads and checks the config. file */
    {
      if (ini_parse(ini, handler, &config) < 0)
        {
          printf("Can't load '%s'\n", ini);
          return 1;
        }

      sanity_check(&config);
      validate(&config, size);
    }

  /* broadcast the input parameters */
  MPI_Bcast(&config, sizeof(configuration), MPI_BYTE, 0, MPI_COMM_WORLD);

  my_proc_row = rank / config.proc_cols;
  my_proc_col = rank % config.proc_cols;

  strong_scaling_flg = (strncmp(config.scaling, "strong", 16) == 0);
  my_rows = strong_scaling_flg ? config.rows/config.proc_rows : config.rows;
  my_cols = strong_scaling_flg ? config.cols/config.proc_cols : config.cols;

  assert((dxpl = H5Pcreate(H5P_DATASET_XFER)) >= 0);
  coll_mpi_io_flg = (strncmp(config.mpi_io, "collective", 16) == 0);
  if (coll_mpi_io_flg)
    assert(H5Pset_dxpl_mpio(dxpl, H5FD_MPIO_COLLECTIVE) >= 0);
  else
    assert(H5Pset_dxpl_mpio(dxpl, H5FD_MPIO_INDEPENDENT) >= 0);

  assert((fapl = H5Pcreate(H5P_FILE_ACCESS)) >= 0);
  assert(H5Pset_fapl_mpio(fapl, MPI_COMM_WORLD, MPI_INFO_NULL) >= 0);
  if (config.alignment_increment > 1)
    assert(H5Pset_alignment(fapl, config.alignment_threshold,
                            config.alignment_increment) >= 0);
  assert(set_libver_bounds(&config, rank, fapl) >= 0);

  if (rank == 0) /* print configuration */
    {
      printf("Config loaded from '%s':\n\tsteps=%d, arrays=%d,"
             "rows=%ld, columns=%ld, scaling=%s\n",
             ini, config.steps, config.arrays, config.rows, config.cols,
             (strong_scaling_flg ? "strong" : "weak"));
      printf("\tproc-grid=%dx%d, slowest-dimension=%s, rank=%d\n",
             config.proc_rows, config.proc_cols, config.slowest_dimension,
             config.rank);
      printf("\talignment-increment=%ld, alignment-threshold=%ld\n",
             config.alignment_increment, config.alignment_threshold);
      printf("\tlayout=%s, fill=%s, mpi-io=%s\n",
             config.layout, config.fill_values, config.mpi_io);
    }

  MPI_Barrier(MPI_COMM_WORLD);

  write_phase = -MPI_Wtime();
  write_test(&config, size, rank, my_proc_row, my_proc_col, my_rows, my_cols,
             fapl, dxpl, &create_time, &write_time);
  write_phase += MPI_Wtime();

  MPI_Barrier(MPI_COMM_WORLD);

  read_phase = -MPI_Wtime();
  read_test(&config, size, rank, my_proc_row, my_proc_col, my_rows, my_cols,
            fapl, dxpl, &read_time);
  read_phase += MPI_Wtime();

  MPI_Barrier(MPI_COMM_WORLD);

  assert(H5Pclose(fapl) >= 0);
  assert(H5Pclose(dxpl) >= 0);

  wall_time += MPI_Wtime();

  if (rank == 0) /* retrieve the file size */
  {
    hid_t file;
    assert((file =
            H5Fopen(config.hdf5_file, H5F_ACC_RDONLY, H5P_DEFAULT)) >= 0);
    assert(H5Fget_filesize(file, &fsize) >= 0);
    assert(H5Fclose(file) >= 0);
  }

  max_write_phase = min_write_phase = 0.0;
  max_create_time = min_create_time = 0.0;
  max_write_time = min_write_time = 0.0;
  max_read_phase = min_read_phase = 0.0;
  max_read_time = min_read_time = 0.0;

  MPI_Reduce(&write_phase, &min_write_phase, 1, MPI_DOUBLE, MPI_MIN, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&write_phase, &max_write_phase, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&create_time, &min_create_time, 1, MPI_DOUBLE, MPI_MIN, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&create_time, &max_create_time, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&write_time, &min_write_time, 1, MPI_DOUBLE, MPI_MIN, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&write_time, &max_write_time, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&read_phase, &min_read_phase, 1, MPI_DOUBLE, MPI_MIN, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&read_phase, &max_read_phase, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&read_time, &min_read_time, 1, MPI_DOUBLE, MPI_MIN, 0,
             MPI_COMM_WORLD);
  MPI_Reduce(&read_time, &max_read_time, 1, MPI_DOUBLE, MPI_MAX, 0,
             MPI_COMM_WORLD);

  if (rank == 0)
    {
      double  byte_count =
        (double)config.steps*config.arrays*my_rows*my_cols*sizeof(double);
      double min_write_rate = byte_count / (1024*1024*max_write_time);
      double max_write_rate = byte_count / (1024*1024*min_write_time);
      double min_read_rate = byte_count / (1024*1024*max_read_time);
      double max_read_rate = byte_count / (1024*1024*min_read_time);
      printf("\nWall clock [s]:\t\t%.2f\n", wall_time);
      printf("File size [B]:\t\t%.0f\n", (double)fsize);
      printf("---------------------------------------------\n");
      printf("Measurement:\t\t_MIN (over MPI ranks)\n");
      printf("\t\t\t^MAX (over MPI ranks)\n");
      printf("---------------------------------------------\n");
      printf("Write phase [s]:\t_%.2f\n\t\t\t^%.2f\n", min_write_phase,
             max_write_phase);
      printf("Create time [s]:\t_%.2f\n\t\t\t^%.2f\n", min_create_time,
             max_create_time);
      printf("Write time [s]:\t\t_%.2f\n\t\t\t^%.2f\n", min_write_time,
             max_write_time);
      printf("Write rate [MiB/s]:\t_%.2f\n\t\t\t^%.2f\n",
             min_write_rate, max_write_rate);
      printf("Read phase [s]:\t\t_%.2f\n\t\t\t^%.2f\n", min_read_phase,
             max_read_phase);
      printf("Read time [s]:\t\t_%.2f\n\t\t\t^%.2f\n", min_read_time,
             max_read_time);
      printf("Read rate [MiB/s]:\t_%.2f\n\t\t\t^%.2f\n",
             min_read_rate, max_read_rate);

      /* write results CSV file */
      FILE *fptr = fopen(config.csv_file, "w");
      assert(fptr != NULL);
      fprintf(fptr, "steps,arrays,rows,cols,scaling,proc-rows,proc-cols,"
                    "slowdim,rank,alignment-increment,alignment-threshold,"
                    "layout,fill,mpi-io,wall [s],fsize [B],"
                    "write-phase-min [s],write-phase-max [s],"
                    "creat-min [s],creat-max [s],"
                    "write-min [s],write-max [s],"
                    "read-phase-min [s],read-phase-max [s],"
                    "read-min [s],read-max [s]\n");
      fprintf(fptr, "%d,%d,%ld,%ld,%s,%d,%d,%s,%d,%ld,%ld,%s,%s,%s,"
                    "%.2f,%.0f,%.2f,%.2f,%.2f,%.2f,"
                    "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
              config.steps, config.arrays, config.rows, config.cols,
              config.scaling, config.proc_rows, config.proc_cols,
              config.slowest_dimension, config.rank,
              config.alignment_increment, config.alignment_threshold,
              config.layout,
              config.fill_values, config.mpi_io, wall_time, (double)fsize,
              min_write_phase, max_write_phase,
              min_create_time, max_create_time,
              min_write_time, max_write_time,
              min_read_phase, max_read_phase,
              min_read_time, max_read_time);
      fclose(fptr);
    }

  MPI_Finalize();

  return 0;
}

herr_t set_libver_bounds(configuration* pconfig, int rank, hid_t fapl)
{
  herr_t result = 0;
  H5F_libver_t low = H5F_LIBVER_EARLIEST, high = H5F_LIBVER_LATEST;
  unsigned majnum, minnum, relnum;
  assert((result = H5get_libversion(&majnum, &minnum, &relnum)) >= 0);
  assert (majnum == 1 && minnum >= 8 && minnum <= 13);

  if (strncmp(pconfig->libver_bound_low, "earliest", 16) != 0)
    {
      if (strncmp(pconfig->libver_bound_low, "v18", 16) == 0)
#if H5_VERSION_GE(1,10,0)
        low = H5F_LIBVER_V18;
#else
        low = H5F_LIBVER_LATEST;
#endif
      else if (strncmp(pconfig->libver_bound_low, "v110", 16) == 0)
#if H5_VERSION_GE(1,12,0)
        low = H5F_LIBVER_V110;
#else
        low = H5F_LIBVER_LATEST;
#endif
      else if (strncmp(pconfig->libver_bound_low, "v112", 16) == 0)
#if H5_VERSION_GE(1,13,0)
        low = H5F_LIBVER_V112;
#else
        low = H5F_LIBVER_LATEST;
#endif
      else
        low = H5F_LIBVER_LATEST;
    }

  if (strncmp(pconfig->libver_bound_high, "latest", 16) != 0)
    {
      if (strncmp(pconfig->libver_bound_high, "v18", 16) == 0)
#if H5_VERSION_GE(1,10,0)
        high = H5F_LIBVER_V18;
#else
        high = H5F_LIBVER_LATEST;
#endif
      else if (strncmp(pconfig->libver_bound_high, "v110", 16) == 0)
#if H5_VERSION_GE(1,12,0)
        high = H5F_LIBVER_V110;
#else
        high = H5F_LIBVER_LATEST;
#endif
      else if (strncmp(pconfig->libver_bound_high, "v112", 16) == 0)
#if H5_VERSION_GE(1,13,0)
        high = H5F_LIBVER_V112;
#else
        high = H5F_LIBVER_LATEST;
#endif
    }

  assert(low <= high);
  assert((result = H5Pset_libver_bounds(fapl, low, high)) >= 0);

  if (rank == 0)
    printf("\nHDF5 library version %d.%d.%d[low=%d,high=%d]\n",
           majnum, minnum, relnum, low, high);

  return result;
}
