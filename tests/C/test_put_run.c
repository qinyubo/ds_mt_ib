/*
 * Copyright (c) 2009, NSF Cloud and Autonomic Computing Center, Rutgers University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided
 * that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this list of conditions and
 * the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided with the distribution.
 * - Neither the name of the NSF Cloud and Autonomic Computing Center, Rutgers University, nor the names of its
 * contributors may be used to endorse or promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
*  Fan Zhang (2013)  TASSL Rutgers University
*  zhangfan@cac.rutgers.edu
*/
#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "mpi.h"

//# of processors in x-y-z direction
static int npx_, npy_, npz_;
//block size per processor per direction
static int spx_, spy_, spz_;
//# of iterations
static int timesteps_;
//# of processors in the application
static int npapp_;

static int rank_, nproc_;

static int offx_, offy_, offz_;

static struct timer timer_;

static MPI_Comm gcomm_;

/*
Matrix representation
+-----> (x)
|
|
v (y)
*/
// TODO(fan): split the function into 2
static double* allocate_2d()
{
	double* tmp = NULL;
	//init off_x_ and off_y_ values
	offx_ = (rank_ % npx_) * spx_;
	offy_ = (rank_ / npx_) * spy_;

	tmp = (double*)malloc(sizeof(double)*spx_*spy_);
	return tmp;
}

static int generate_2d(double *m2d, unsigned int ts)
{
	// double value = 1.0*(rank_) + 0.0001*ts;
	double value = ts;
	int m2d_size = spx_ * spy_;
	int i;
	for(i=0; i<m2d_size; i++){
		*(m2d+i) = value;
	}

	return 0;
}

static int couple_write_2d_multi_var(unsigned int ts,
				enum transport_type type, int num_vars)
{
        char var_name[128];
        double *data = NULL;
        int i;

        common_lock_on_write("m2d_lock", &gcomm_);

        //put the m2d into the space
        int elem_size = sizeof(double);
        /*set the two coordinates for the box*/
        int xl = offx_;
        int yl = offy_;
        int zl = 0;
        int xu = offx_ + spx_ - 1;
        int yu = offy_ + spy_ - 1;
        int zu = 0;
        double tm_st, tm_end1, tm_end2;

        uloga("TS=%u, %d write m2d:{(%d,%d,%d),(%d,%d,%d)} into space\n",
                ts, rank_, xl,yl,zl,xu,yu,zu);

        MPI_Barrier(gcomm_);
        tm_st = timer_read(&timer_);

        for (i = 0; i < num_vars; i++) {
                sprintf(var_name, "m2d_%d", i);
                data = allocate_2d();
                if (data == NULL) {
                        uloga("%s(): failed to alloc buffer, i=%d\n",
                                __func__, i);
                        return -1;
                }

                generate_2d(data, ts);

                common_put(var_name, ts, elem_size,
                        xl, yl, zl, xu, yu, zu,
                        data, type);

                if (type == USE_DSPACES) {
                        common_put_sync(type);
                }

                if (data)
                        free(data);

        }

        if ( type == USE_DSPACES ) {
                tm_end1 = timer_read(&timer_);
                MPI_Barrier(gcomm_);
                tm_end2 = timer_read(&timer_);

                common_unlock_on_write("m2d_lock", &gcomm_);
        } else if (type == USE_DIMES) {
                tm_end1 = timer_read(&timer_);
                MPI_Barrier(gcomm_);
                tm_end2 = timer_read(&timer_);

		sleep(2);
                common_unlock_on_write("m2d_lock", &gcomm_);
		common_put_sync(type);
        }

        uloga("TS= %u TRANSPORT_TYPE= %d RANK= %d write time= %lf\n",
                ts, type, rank_, tm_end1-tm_st);
        if (rank_ == 0) {
                uloga("TS= %u TRANSPORT_TYPE= %d write MAX time= %lf\n",
                        ts, type, tm_end2-tm_st);
        }

        return 0;
}

static int couple_write_2d(double *m2d, unsigned int ts, enum transport_type type)
{
	common_lock_on_write("m2d_lock", &gcomm_);

	//put the m2d into the space
	int elem_size = sizeof(double);
	/*set the two coordinates for the box*/
	int xl = offx_;
	int yl = offy_;
	int zl = 0;
	int xu = offx_ + spx_ - 1;
	int yu = offy_ + spy_ - 1;
	int zu = 0;
	double tm_st, tm_end1, tm_end2;

	uloga("Timestep=%u, %d write m2d:{(%d,%d,%d),(%d,%d,%d)} into space\n",
		ts, rank_, xl,yl,zl,xu,yu,zu);

	MPI_Barrier(gcomm_);
	tm_st = timer_read(&timer_);

	common_put("m2d", ts, elem_size, 
		xl, yl, zl, xu, yu, zu,
		m2d, type);

	if ( type == USE_DSPACES ) {
		common_put_sync(type);

		tm_end1 = timer_read(&timer_);
		MPI_Barrier(gcomm_);
		tm_end2 = timer_read(&timer_);

		common_unlock_on_write("m2d_lock", &gcomm_);
	} else if (type == USE_DIMES) {
		tm_end1 = timer_read(&timer_);
		MPI_Barrier(gcomm_);
		tm_end2 = timer_read(&timer_);

		sleep(2);
		common_unlock_on_write("m2d_lock", &gcomm_);
		common_put_sync(type);
	}

	uloga("TS= %u TRANSPORT_TYPE= %d RANK= %d write time= %lf\n",
		ts, type, rank_, tm_end1-tm_st);
	if (rank_ == 0) {
		uloga("TS= %u TRANSPORT_TYPE= %d write MAX time= %lf\n",
			ts, type, tm_end2-tm_st);
	}

	return 0;
}

int test_put_run(int num_ts,int num_process,int process_x,int process_y,
	int process_z,int dims,int dim_x,int dim_y,int dim_z,MPI_Comm gcomm)
{
	gcomm_ = gcomm;
	timesteps_ = num_ts;
	npapp_ = num_process;
	npx_ = process_x;
	npy_ = process_y;
	npz_ = process_z;
	if(process_x)
		spx_ = dim_x/process_x;
	if(process_y)
		spy_ = dim_y/process_y;
	if(process_z)
		spz_ = dim_z/process_z;

	timer_init(&timer_, 1);
	timer_start(&timer_);

	int app_id = 1;
	common_init(npapp_, app_id);
	common_set_storage_type(row_major);
	rank_ = common_rank();
	nproc_ = common_peers();

	double *m2d = NULL;
	m2d = allocate_2d();
	if (m2d) {
		unsigned int ts;
		for (ts = 1; ts <= timesteps_; ts++){
			generate_2d(m2d, ts);
			if (ts % 2 == 0)		
				couple_write_2d(m2d, ts, USE_DIMES);
				//couple_write_2d_multi_var(ts, USE_DIMES, 10);
			else if (ts % 2 == 1)
				couple_write_2d(m2d, ts, USE_DSPACES);
				//couple_write_2d_multi_var(ts, USE_DSPACES, 10);
		}
	}

	common_barrier();
	common_finalize();

	if (m2d)
		free(m2d);
	
	return 0;	
}
