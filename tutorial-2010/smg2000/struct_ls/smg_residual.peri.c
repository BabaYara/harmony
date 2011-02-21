/*BHEADER**********************************************************************
  * (c) 1997   The Regents of the University of California
 *
 * See the file COPYRIGHT_and_DISCLAIMER for a complete copyright
 * notice, contact person, and disclaimer.
 *
 * $Revision: 2.0 $
 *********************************************************************EHEADER*/
/******************************************************************************
 *
 * Routine for computing residuals in the SMG code
 *
 *****************************************************************************/
#include "headers.h"
#include <assert.h>
//using namespace std;

#define MAX_STENCIL 15
#define TAG 1234
#ifdef ACTIVE_HARMONY
#include "hclientc.h"
//#include "hsockutil.h"
#endif
#ifdef USE_DLOPEN
#include  <dlfcn.h>
static int g_execution_flag =0;
static int so_file_opened = 0;

static int num_evaluations = 0;
static double time_residual = 0.0;
extern double time_stamp();
void * FunctionLib; /* Handle to the shared lib file */
void * FLibBest; // best seen so far
static int perf_multiplier = 10000;
/*  pointer to a loaded routine */
void (*OUT__1__6119__)(void **__out_argv);
char cmd[256];
const char *dlError;    /* Pointer to error string */
static float perf_f=0.0;
#endif

#ifdef ACTIVE_HARMONY
static int har_registration = 0;
static int default_performance_sent = 0;
static int *TI=NULL;
static int *TJ=NULL;
static int *TK=NULL;
static int *UI=NULL; 
static int *US=NULL;
static int buffer[1];

static int harmony_ended = 0;
static int best_seen_so_far = -1;
static char best_lib_name[255];
static int best_lib_changed = 0;
extern double time_stamp();
static int measuring_phase = 0;
static int iters_passed = 0;
static int num_iters_measured = 400;
static int perf = 0;

static int code_complete=0;
static int check_code=0;
static int num_iters_wait=400;
static int search_iter=1;
#endif



/*--------------------------------------------------------------------------
 * hypre_SMGResidualData data structure
 *--------------------------------------------------------------------------*/
typedef struct __unnamed_class___F0_L23_C9__variable_declaration__variable_type_L13R_variable_name_base_index__DELIMITER___variable_declaration__variable_type_L13R_variable_name_base_stride__DELIMITER___variable_declaration__variable_type___Pb__L14R__Pe___variable_name_A__DELIMITER___variable_declaration__variable_type___Pb__L15R__Pe___variable_name_x__DELIMITER___variable_declaration__variable_type___Pb__L15R__Pe___variable_name_b__DELIMITER___variable_declaration__variable_type___Pb__L15R__Pe___variable_name_r__DELIMITER___variable_declaration__variable_type___Pb__L16R__Pe___variable_name_base_points__DELIMITER___variable_declaration__variable_type___Pb__L17R__Pe___variable_name_compute_pkg__DELIMITER___variable_declaration__variable_type_i_variable_name_time_index__DELIMITER___variable_declaration__variable_type_i_variable_name_flops {
    hypre_Index base_index;
    hypre_Index base_stride;
    hypre_StructMatrix *A;
    hypre_StructVector *x;
    hypre_StructVector *b;
    hypre_StructVector *r;
    hypre_BoxArray *base_points;
    hypre_ComputePkg *compute_pkg;
    int time_index;
    int flops;}hypre_SMGResidualData;
/*--------------------------------------------------------------------------
 * hypre_SMGResidualCreate
 *--------------------------------------------------------------------------*/

void *hypre_SMGResidualCreate()
{
    hypre_SMGResidualData *residual_data;
    residual_data = ((hypre_SMGResidualData *)(hypre_CAlloc((((unsigned int )1)),(((unsigned int )(sizeof(hypre_SMGResidualData )))))));
    residual_data -> time_index = hypre_InitializeTiming("SMGResidual");
/* set defaults */
    (((((residual_data -> base_index)[0] = 0) , ((residual_data -> base_index)[1] = 0))) , ((residual_data -> base_index)[2] = 0));
    (((((residual_data -> base_stride)[0] = 1) , ((residual_data -> base_stride)[1] = 1))) , ((residual_data -> base_stride)[2] = 1));
    return (void *)residual_data;
}

/*--------------------------------------------------------------------------
 * hypre_SMGResidualSetup
 *--------------------------------------------------------------------------*/

int hypre_SMGResidualSetup(void *residual_vdata,hypre_StructMatrix *A,hypre_StructVector *x,hypre_StructVector *b,hypre_StructVector *r)
{
    int ierr = 0;
    hypre_SMGResidualData *residual_data = (hypre_SMGResidualData *)residual_vdata;
    hypre_IndexRef base_index = (residual_data -> base_index);
    hypre_IndexRef base_stride = (residual_data -> base_stride);
    hypre_Index unit_stride;
    hypre_StructGrid *grid;
    hypre_StructStencil *stencil;
    hypre_BoxArrayArray *send_boxes;
    hypre_BoxArrayArray *recv_boxes;
    int **send_processes;
    int **recv_processes;
    hypre_BoxArrayArray *indt_boxes;
    hypre_BoxArrayArray *dept_boxes;
    hypre_BoxArray *base_points;
    hypre_ComputePkg *compute_pkg;
/*----------------------------------------------------------
 * Set up base points and the compute package
 *----------------------------------------------------------*/
    grid = (A -> grid);
    stencil = (A -> stencil);
    ((((unit_stride[0] = 1) , (unit_stride[1] = 1))) , (unit_stride[2] = 1));
    base_points = hypre_BoxArrayDuplicate((grid -> boxes));
    hypre_ProjectBoxArray(base_points,base_index,base_stride);
    hypre_CreateComputeInfo(grid,stencil,&send_boxes,&recv_boxes,&send_processes,&recv_processes,&indt_boxes,&dept_boxes);
    hypre_ProjectBoxArrayArray(indt_boxes,base_index,base_stride);
    hypre_ProjectBoxArrayArray(dept_boxes,base_index,base_stride);
    hypre_ComputePkgCreate(send_boxes,recv_boxes,unit_stride,unit_stride,send_processes,recv_processes,indt_boxes,dept_boxes,base_stride,grid,(x -> data_space),1,&compute_pkg);
/*----------------------------------------------------------
 * Set up the residual data structure
 *----------------------------------------------------------*/
    residual_data -> A = hypre_StructMatrixRef(A);
    residual_data -> x = hypre_StructVectorRef(x); 
    residual_data -> b = hypre_StructVectorRef(b);
    residual_data -> r = hypre_StructVectorRef(r);
    residual_data -> base_points = base_points;
    residual_data -> compute_pkg = compute_pkg;
/*-----------------------------------------------------
 * Compute flops
 *-----------------------------------------------------*/
    residual_data -> flops = (((A -> global_size) + (x -> global_size)) / (((base_stride[0]) * (base_stride[1])) * (base_stride[2])));
    return ierr;
}

/*--------------------------------------------------------------------------
 * hypre_SMGResidual
 *--------------------------------------------------------------------------*/
// helpers
int getoneline(FILE *in, char *buf) {

    int i;
    char c;
    if((c=fgetc(in))==EOF)
        return EOF;
    else
        ungetc(c,in);
    for(i=0;;i++) {
        buf[i]=fgetc(in);
        if(buf[i]=='\n')
            break;
        else if(buf[i]==EOF) {
            ungetc(EOF,in);
            break;
        }
    }

    buf[i]='\0';
    return 0;
}

int getline(FILE *in, char *buf) {

    int r;

    while(1) {
        r=getoneline(in,buf);
        if(r==EOF)
            return r;
        if(buf[0]=='\0')        /* empty line */
            continue;
        else if(buf[0]=='#')    /* comment line */
            continue;
        return r;
    }
}
int hypre_SMGResidual(void *residual_vdata,hypre_StructMatrix *A,hypre_StructVector *x,hypre_StructVector *b,hypre_StructVector *r)
{
    int ierr = 0;
    hypre_SMGResidualData *residual_data = (hypre_SMGResidualData *)residual_vdata;
    hypre_IndexRef base_stride = (residual_data -> base_stride);
    hypre_BoxArray *base_points = (residual_data -> base_points);
    hypre_ComputePkg *compute_pkg = (residual_data -> compute_pkg);
    hypre_CommHandle *comm_handle;
    hypre_BoxArrayArray *compute_box_aa;
    hypre_BoxArray *compute_box_a;
    hypre_Box *compute_box;
    hypre_Box *A_data_box;
    hypre_Box *x_data_box;
    hypre_Box *b_data_box;
    hypre_Box *r_data_box;
    int Ai;
    int xi;
    int bi;
    int ri;
    double *Ap;
    double *xp;
    double *bp;
    double *rp;
    hypre_Index loop_size;
    hypre_IndexRef start;
    hypre_StructStencil *stencil;
    hypre_Index *stencil_shape;
    int stencil_size;
    int compute_i;
    int i;
    int j;
    int si;
    int loopi;
    int loopj;
    int loopk;

    int myrank, comm_size;
    MPI_Status recv_status;
    MPI_Comm_rank(MPI_COMM_WORLD, &myrank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    //printf("myrank is : %d \n", myrank);


#ifdef ACTIVE_HARMONY
    // register the tunable variable, only for the first time
    if(har_registration == 0)
    {
        printf("Connecting to the Harmony server...\n");
        c_harmony_startup(0);
        printf("Sending variable description file!\n");
        sprintf(cmd, "smg2000.tcl");

        //harmony_application_setup_file("smg2000.tcl");
        c_harmony_application_setup_file(cmd);

        printf("Adding harmony variables ... \n");
        TI=(int *)c_harmony_add_variable("smg","TI",1);
        TJ=(int *)c_harmony_add_variable("smg","TJ",1);
        TK=(int *)c_harmony_add_variable("smg","TK",1);
        UI=(int *)c_harmony_add_variable("smg","UI",1);
        US=(int *)c_harmony_add_variable("smg","US",1);

        //harmony_performance_update();
        har_registration++;
        printf("Registration Complete ... \n");
        MPI_Barrier(MPI_COMM_WORLD); // let everyone catch up
        //harmony_request_all();
    }
#endif
 


/* New static variables, precomputed */
    hypre_BeginTiming((residual_data -> time_index));
    stencil = (A -> stencil);
    stencil_shape = (stencil -> shape);
    stencil_size = (stencil -> size);
    (stencil_size <= 15)?0 : ((__assert_fail(("stencil_size <= 15"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(203),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
    for (compute_i = 0; compute_i < 2; compute_i++) {
        switch(compute_i){
            case 0:
            {
                {
                    xp = (x -> data);
                    hypre_InitializeIndtComputations(compute_pkg,xp,&comm_handle);
                    compute_box_aa = (compute_pkg -> indt_boxes);
                    compute_box_a = base_points;
                    for (i = 0; i < (compute_box_a -> size); i++) {
                        compute_box = ((compute_box_a -> boxes) + i);
                        start = (compute_box -> imin);
                        b_data_box = ((( *(b -> data_space)).boxes) + i);
                        r_data_box = ((( *(r -> data_space)).boxes) + i);
                        bp = ((b -> data) + ((b -> data_indices)[i]));
                        rp = ((r -> data) + ((r -> data_indices)[i]));
                        hypre_BoxGetStrideSize(compute_box,base_stride,loop_size);
                        {
                            int hypre__i1start = (((start[0]) - ((b_data_box -> imin)[0])) + ((((start[1]) - ((b_data_box -> imin)[1])) + (((start[2]) - ((b_data_box -> imin)[2])) * (((0 < ((((b_data_box -> imax)[1]) - ((b_data_box -> imin)[1])) + 1))?((((b_data_box -> imax)[1]) - ((b_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1))?((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1) : 0))));
                            int hypre__i2start = (((start[0]) - ((r_data_box -> imin)[0])) + ((((start[1]) - ((r_data_box -> imin)[1])) + (((start[2]) - ((r_data_box -> imin)[2])) * (((0 < ((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1))?((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0))));
                            int hypre__sx1 = (base_stride[0]);
                            int hypre__sy1 = ((base_stride[1]) * (((0 < ((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1))?((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1) : 0)));
                            int hypre__sz1 = (((base_stride[2]) * (((0 < ((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1))?((((b_data_box -> imax)[0]) - ((b_data_box -> imin)[0])) + 1) : 0))) * (((0 < ((((b_data_box -> imax)[1]) - ((b_data_box -> imin)[1])) + 1))?((((b_data_box -> imax)[1]) - ((b_data_box -> imin)[1])) + 1) : 0)));
                            int hypre__sx2 = (base_stride[0]);
                            int hypre__sy2 = ((base_stride[1]) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0)));
                            int hypre__sz2 = (((base_stride[2]) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0))) * (((0 < ((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1))?((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1) : 0)));
                            int hypre__nx = (loop_size[0]);
                            int hypre__ny = (loop_size[1]);
                            int hypre__nz = (loop_size[2]);
                            int hypre__mx = hypre__nx;
                            int hypre__my = hypre__ny;
                            int hypre__mz = hypre__nz;
                            int hypre__dir;
                            int hypre__max;
                            int hypre__div;
                            int hypre__mod;
                            int hypre__block;
                            int hypre__num_blocks;
                            hypre__dir = 0;
                            hypre__max = hypre__nx;
                            if (hypre__ny > hypre__max) {
                                hypre__dir = 1;
                                hypre__max = hypre__ny;
                            }
                            if (hypre__nz > hypre__max) {
                                hypre__dir = 2;
                                hypre__max = hypre__nz;
                            }
                            hypre__num_blocks = 1;
                            if (hypre__max < hypre__num_blocks) {
                                hypre__num_blocks = hypre__max;
                            }
                            if (hypre__num_blocks > 0) {
                                hypre__div = (hypre__max / hypre__num_blocks);
                                hypre__mod = (hypre__max % hypre__num_blocks);
                            }
/* # 236 "smg_residual.c" */
                            (hypre__sx1 == 1)?0 : ((__assert_fail(("hypre__sx1 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(357),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                            (hypre__sx2 == 1)?0 : ((__assert_fail(("hypre__sx2 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(358),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                            if (hypre__num_blocks == 1) {
                                int ii;
                                int jj;
                                int kk;
                                const double *bp_0 = (bp + hypre__i1start);
                                double *rp_0 = (rp + hypre__i2start);
                                for (kk = 0; kk < hypre__mz; kk++) {
                                    for (jj = 0; jj < hypre__my; jj++) {
                                        const double *bpp = ((bp_0 + (jj * hypre__sy1)) + (kk * hypre__sz1));
                                        double *rpp = ((rp_0 + (jj * hypre__sy2)) + (kk * hypre__sz2));
                                        for (ii = 0; ii < hypre__mx; ii++) {
                                            rpp[ii] = (bpp[ii]);
                                        }
                                    }
                                }
/* hypre__num_blocks > 1 */
                            }
                            else {
                                for (hypre__block = 0; hypre__block < hypre__num_blocks; hypre__block++) {
                                    loopi = 0;
                                    loopj = 0;
                                    loopk = 0;
                                    hypre__nx = hypre__mx;
                                    hypre__ny = hypre__my;
                                    hypre__nz = hypre__mz;
                                    if (hypre__dir == 0) {
                                        loopi = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                        hypre__nx = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                    }
                                    else if (hypre__dir == 1) {
                                        loopj = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                        hypre__ny = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                    }
                                    else if (hypre__dir == 2) {
                                        loopk = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                        hypre__nz = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                    }
                                    bi = (((hypre__i1start + loopi) + (loopj * hypre__sy1)) + (loopk * hypre__sz1));
                                    ri = (((hypre__i2start + loopi) + (loopj * hypre__sy2)) + (loopk * hypre__sz2));
/* AAA */
                                    {
                                        int ii;
                                        int jj;
                                        int kk;
                                        const double *bp_0 = (bp + bi);
                                        double *rp_0 = (rp + ri);
                                        for (kk = 0; kk < hypre__nz; kk++) {
                                            for (jj = 0; jj < hypre__ny; jj++) {
                                                const double *bpp = ((bp_0 + (jj * hypre__sy1)) + (kk * hypre__sz1));
                                                double *rpp = ((rp_0 + (jj * hypre__sy2)) + (kk * hypre__sz2));
                                                for (ii = 0; ii < hypre__nx; ii++) {
                                                    rpp[ii] = (bpp[ii]);
                                                }
                                            }
                                        }
/* AAA */
                                    }
                                }
/* hypre__num_blocks > 1 */
                            }
                        }
                    }
                }
                break;
            }
            case 1:
            {
                {
                    hypre_FinalizeIndtComputations(comm_handle);
                    compute_box_aa = (compute_pkg -> dept_boxes);
                }
                break;
            }
/* switch */
        }
/*--------------------------------------------------------------------
 * Compute r -= A*x
 *--------------------------------------------------------------------*/
        for (i = 0; i < (compute_box_aa -> size); i++) {
            int dxp_s[15UL];
            compute_box_a = ((compute_box_aa -> box_arrays)[i]);
            A_data_box = ((( *(A -> data_space)).boxes) + i);
            x_data_box = ((( *(x -> data_space)).boxes) + i);
            r_data_box = ((( *(r -> data_space)).boxes) + i);
            rp = ((r -> data) + ((r -> data_indices)[i]));
            for (si = 0; si < stencil_size; si++) {
                dxp_s[si] = (((stencil_shape[si])[0]) + ((((stencil_shape[si])[1]) + (((stencil_shape[si])[2]) * (((0 < ((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1))?((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1))?((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1) : 0))));
            }
            for (j = 0; j < (compute_box_a -> size); j++) {{
                    int hypre__i1start;
                    int hypre__i2start;
                    int hypre__i3start;
                    int hypre__sx1;
                    int hypre__sy1;
                    int hypre__sz1;
                    int hypre__sx2;
                    int hypre__sy2;
                    int hypre__sz2;
                    int hypre__sx3;
                    int hypre__sy3;
                    int hypre__sz3;
                    int hypre__nx;
                    int hypre__ny;
                    int hypre__nz;
                    int hypre__mx;
                    int hypre__my;
                    int hypre__mz;
                    int hypre__dir;
                    int hypre__max;
                    int hypre__div;
                    int hypre__mod;
                    int hypre__block;
                    int hypre__num_blocks;
                    compute_box = ((compute_box_a -> boxes) + j);
                    start = (compute_box -> imin);
                    hypre__i1start = (((start[0]) - ((A_data_box -> imin)[0])) + ((((start[1]) - ((A_data_box -> imin)[1])) + (((start[2]) - ((A_data_box -> imin)[2])) * (((0 < ((((A_data_box -> imax)[1]) - ((A_data_box -> imin)[1])) + 1))?((((A_data_box -> imax)[1]) - ((A_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1))?((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1) : 0))));
                    hypre__i2start = (((start[0]) - ((x_data_box -> imin)[0])) + ((((start[1]) - ((x_data_box -> imin)[1])) + (((start[2]) - ((x_data_box -> imin)[2])) * (((0 < ((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1))?((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1))?((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1) : 0))));
                    hypre__i3start = (((start[0]) - ((r_data_box -> imin)[0])) + ((((start[1]) - ((r_data_box -> imin)[1])) + (((start[2]) - ((r_data_box -> imin)[2])) * (((0 < ((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1))?((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1) : 0)))) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0))));
                    hypre_BoxGetStrideSize(compute_box,base_stride,loop_size);
                    hypre__sx1 = (base_stride[0]);
                    hypre__sy1 = ((base_stride[1]) * (((0 < ((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1))?((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1) : 0)));
                    hypre__sz1 = (((base_stride[2]) * (((0 < ((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1))?((((A_data_box -> imax)[0]) - ((A_data_box -> imin)[0])) + 1) : 0))) * (((0 < ((((A_data_box -> imax)[1]) - ((A_data_box -> imin)[1])) + 1))?((((A_data_box -> imax)[1]) - ((A_data_box -> imin)[1])) + 1) : 0)));
                    hypre__sx2 = (base_stride[0]);
                    hypre__sy2 = ((base_stride[1]) * (((0 < ((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1))?((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1) : 0)));
                    hypre__sz2 = (((base_stride[2]) * (((0 < ((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1))?((((x_data_box -> imax)[0]) - ((x_data_box -> imin)[0])) + 1) : 0))) * (((0 < ((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1))?((((x_data_box -> imax)[1]) - ((x_data_box -> imin)[1])) + 1) : 0)));
                    hypre__sx3 = (base_stride[0]);
                    hypre__sy3 = ((base_stride[1]) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0)));
                    hypre__sz3 = (((base_stride[2]) * (((0 < ((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1))?((((r_data_box -> imax)[0]) - ((r_data_box -> imin)[0])) + 1) : 0))) * (((0 < ((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1))?((((r_data_box -> imax)[1]) - ((r_data_box -> imin)[1])) + 1) : 0)));
/* Based on BG/L Milestone #46 */
                    (hypre__sx1 == 1)?0 : ((__assert_fail(("hypre__sx1 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(602),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                    (hypre__sx2 == 1)?0 : ((__assert_fail(("hypre__sx2 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(603),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                    (hypre__sx3 == 1)?0 : ((__assert_fail(("hypre__sx3 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(604),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                    hypre__mx = (loop_size[0]);
                    hypre__my = (loop_size[1]);
                    hypre__mz = (loop_size[2]);
                    hypre__dir = 0;
                    hypre__max = hypre__mx;
                    if (hypre__my > hypre__max) {
                        hypre__dir = 1;
                        hypre__max = hypre__my;
                    }
                    if (hypre__mz > hypre__max) {
                        hypre__dir = 2;
                        hypre__max = hypre__mz;
                    }
                    hypre__num_blocks = 1;
                    if (hypre__max < hypre__num_blocks) {
                        hypre__num_blocks = hypre__max;
                    }
                    if (hypre__num_blocks > 0) {
                        hypre__div = (hypre__max / hypre__num_blocks);
                        hypre__mod = (hypre__max % hypre__num_blocks);
                    }
                    else
                        continue;
                    if (hypre__num_blocks == 1) {
                        int si;
                        int ii;
                        int jj;
                        int kk;
                        const double *Ap_0 = ((A -> data) + hypre__i1start);
                        const double *xp_0 = (((x -> data) + hypre__i2start) + ((x -> data_indices)[i]));
                        ri = hypre__i3start;
                        double time1__, time2__;
                        time1__=time_stamp();
                        void *__out_argv1__1527__[21];
                        *(__out_argv1__1527__ + 0) = ((void *)(&xp_0));
                        *(__out_argv1__1527__ + 1) = ((void *)(&Ap_0));
                        *(__out_argv1__1527__ + 2) = ((void *)(&kk));
                        *(__out_argv1__1527__ + 3) = ((void *)(&jj));
                        *(__out_argv1__1527__ + 4) = ((void *)(&ii));
                        *(__out_argv1__1527__ + 5) = ((void *)(&si));
                        *(__out_argv1__1527__ + 6) = ((void *)(&hypre__mz));
                        *(__out_argv1__1527__ + 7) = ((void *)(&hypre__my));
                        *(__out_argv1__1527__ + 8) = ((void *)(&hypre__mx));
                        *(__out_argv1__1527__ + 9) = ((void *)(&hypre__sz3));
                        *(__out_argv1__1527__ + 10) = ((void *)(&hypre__sy3));
                        *(__out_argv1__1527__ + 11) = ((void *)(&hypre__sz2));
                        *(__out_argv1__1527__ + 12) = ((void *)(&hypre__sy2));
                        *(__out_argv1__1527__ + 13) = ((void *)(&hypre__sz1));
                        *(__out_argv1__1527__ + 14) = ((void *)(&hypre__sy1));
                        *(__out_argv1__1527__ + 15) = ((void *)(&dxp_s));
                        *(__out_argv1__1527__ + 16) = ((void *)(&i));
                        *(__out_argv1__1527__ + 17) = ((void *)(&stencil_size));
                        *(__out_argv1__1527__ + 18) = ((void *)(&rp));
                        *(__out_argv1__1527__ + 19) = ((void *)(&ri));
                        *(__out_argv1__1527__ + 20) = ((void *)(&A));
                        //printf("checkpoint 1 \n");
                        //if((myrank == 0) || (myrank == 2) || (myrank==5))
                        //    printf( "%d %d %d  %d  %d \n", myrank, stencil_size, hypre__mz, hypre__my, hypre__mx);
                        //if(myrank == 0)
                        //    printf( "%d %d  %d  %d \n", stencil_size, hypre__mz, hypre__my, hypre__mx);
                        int trigger = (stencil_size == 7) && (hypre__mz == 100) && (hypre__my == 100) && (hypre__mx==100);
                        //if(trigger == 1)
                        //    printf("found a trigger at num_evaluation %d \n", num_evaluations);

//#ifdef USE_DLOPEN
                        //if (g_execution_flag == 0){
                        //if(so_file_opened == 0)

                        if ( so_file_opened == 0)
                        {  
                            
                            char filename[255];
                            //sprintf(filename, "/hivehomes/tiwari/smg2000_harmony/test/code/OUT__1__6119__");
                            sprintf(filename,
                            "/hivehomes/tiwari/smg2000/test/OUT__1__6119__.chun.default.so");
#ifdef POSTHARMONY
                            FILE *tmp_f;
                            tmp_f=fopen("/hivehomes/tiwari/smg2000/test/best_code_variant.txt", "r");
                            getline(tmp_f, filename);
                            fclose(tmp_f);
#endif 

                            printf("Opening the .so file ... %s \n", filename);
                            FunctionLib = dlopen(filename,RTLD_LAZY);
                            FLibBest = dlopen(filename,RTLD_LAZY);

                            dlError = dlerror();  
                            if( dlError ) {
                                printf("cannot open .so file!\n");
                                exit(1);
                            }
                            /* Find the first loaded function */
                            OUT__1__6119__ = (void (*)(void**))dlsym( FunctionLib, "OUT__1__6119__");
                            dlError = dlerror();
                            if( dlError )
                            {
                                printf("cannot find OUT__1__6755__() !\n");
                                exit(1);
                            }
                            so_file_opened = 1;
                        }

                        MPI_Barrier(MPI_COMM_WORLD);
#ifdef ACTIVE_HARMONY
                        if(trigger && !harmony_ended)
                        {
                            printf("found a trigger at num_evaluation %d \n", num_evaluations);

                            if(default_performance_sent)
                            {
                                // check if the code is ready for the new
                                // set of parameters
                                if(myrank == 0)
                                {
                                    code_complete = c_code_generation_complete(search_iter);
                                    buffer[0] = code_complete;
                                    printf("Sending code completion Message to Children %d \n", code_complete);
                                    // send this to other processors
                                    int proc_no;
                                    for(proc_no = 1; proc_no < comm_size; proc_no++)
                                        MPI_Send(buffer, 1, MPI_INT, proc_no, TAG, MPI_COMM_WORLD);

                                    buffer[0]=0;
                                } else
                                {
                                    MPI_Recv(buffer, 1, MPI_INT, 0, TAG, MPI_COMM_WORLD, &recv_status);
                                    code_complete=buffer[0];
                                    buffer[0]=0;
                                    printf("Received code completion message from parent %d \n", code_complete);
                                }
                                MPI_Barrier(MPI_COMM_WORLD);
                                if(code_complete == 1)
                                {
                                    // code is complete
                                    // get the params and load new code
                                    c_harmony_request_all();
                                    char filename[255];
                                    //	/hivehomes/tiwari/SC09-demo/smg2000/test/OUT__1__6119__.chun.default.so
                                    //sprintf(filename, "/hivehomes/tiwari/SC09-demo/smg2000/test/OUT__1__6119__.chun.default.so");
                                    
                                    //sprintf(filename,
                                    //"/hivehomes/tiwari/smg2000_harmony/test/code/OUT__1__6119__%d_%d_%d_%d_%d.so",
                                    //*TI, *TJ,*TK, *UI,*US);
                                    sprintf(filename,
                                    "/hivehomes/tiwari/scratch/code/OUT__1__6119__%d_%d_%d_%d_%d.so",
                                    *TI, *TJ,*TK, *UI,*US);

                                    FunctionLib = dlopen(filename, RTLD_LAZY);
                                    dlError = dlerror();
                                    if( dlError ) {
                                        printf("cannot open .so file!\n");
                                        exit(1);
                                    }
                                    OUT__1__6119__ = (void (*) (void**)) dlsym( FunctionLib, "OUT__1__6119__");
                                    dlError = dlerror();
                                    printf("rank %d Just loaded : %s \n", myrank, filename);

                                    if( dlError )
                                    {
                                        printf("cannot find OUT__1__6755__() !\n");
                                        exit(1);
                                    }

                                    // evaluate

                                    time1__=time_stamp();
                                    (*OUT__1__6119__)(__out_argv1__1527__);
                                    time2__=time_stamp();
                                    perf_f=(time2__-time1__);
                                    perf = perf_f*perf_multiplier;
                                    printf("performance for the trigger point: %f, %d \n", perf_f, perf);
                                    // keeping track of the best
                                    if(perf < best_seen_so_far)
                                    {
                                        best_seen_so_far=perf;
                                        sprintf(best_lib_name, "%s",filename);
                                        printf("best lib name set to %s \n", best_lib_name);
                                        best_lib_changed=1;
                                        FLibBest =dlopen(filename, RTLD_LAZY);
                                        dlError = dlerror();
                                        if( dlError ) {
                                            printf("cannot update the best lib %s !\n", filename);
                                            exit(1);
                                        }
                                        //FLibBest = FunctionLib;

                                    } else
                                    {
                                        best_lib_changed = 0;
                                    }
                                    // send the performance update to the server
                                    c_harmony_performance_update(perf);
                                    
                                    /*
                                    // check if the search is complete
                                    harmony_ended=harmony_request_tcl_variable("all_done", VAR_INT);
                                    // if the search converged, unregister
                                    // with the harmony server
                                    if(harmony_ended == 1)
                                        harmony_end();
                                    */

                                    search_iter++;
                                } else
                                {

                                    // code generation is not complete,
                                    //   load the best code that we have
                                    // seen so far
                                    printf("code-generation is not complete: loading the best \n");
                                    OUT__1__6119__ = (void (*) (void**)) dlsym( FLibBest, "OUT__1__6119__");
                                    (*OUT__1__6119__)(__out_argv1__1527__);
                                }
                            } else
                            {
                                // default performance has not been sent
                                // yet
                                printf(" default performance has not been sent yet. do so. \n");
                                
                                time1__=time_stamp();
                                (*OUT__1__6119__)(__out_argv1__1527__);
                                time2__=time_stamp();
                                perf_f=time2__-time1__;
                                perf = perf_f * perf_multiplier;
                                printf("Default performance for the trigger point: %d \n", perf);
                               
                                if(default_performance_sent == 0)
                                {
                                    perf=perf_f * perf_multiplier;
                                    c_harmony_performance_update(perf);
                                    //harmony_request_all();
                                    default_performance_sent = 1;
                                }
                                best_seen_so_far=perf;
                            }
                        } else
                        {
                            // haven't reached another trigger yet. 
                            // or we have reached the end of the search process.
                            // In both cases, continue with the best.
                            //printf("we are either done or the code is not complete yet \n");
                            OUT__1__6119__ = (void (*) (void**)) dlsym( FLibBest, "OUT__1__6119__");
                            (*OUT__1__6119__)(__out_argv1__1527__);
                        }
#else
                        // if harmony is not used
                        OUT__1__6119__ = (void (*) (void**)) dlsym( FLibBest, "OUT__1__6119__");
                         time1__=time_stamp();
                        OUT__1__6119__(__out_argv1__1527__);
                        time2__=time_stamp();
                        perf_f=time2__-time1__;
                        int perf = perf_f * perf_multiplier;
                        if(trigger)
                            printf("Default performance for the trigger point: %f, %d \n", perf_f, perf);
#endif
                        num_evaluations++;




                        /*

                        if(measuring_phase == 1)
                        {
                            //printf("measuring phase %d \n", iters_passed);

                            if(iters_passed == num_iters_measured)
                            {
                                //printf("inside the measuring phase \n");

                                perf=perf_f * perf_multiplier;
                                harmony_performance_update(perf);
                                search_iter++;
                                printf("performance after 100 iterations %f \n", perf_f);

                                perf_f=0;

                                measuring_phase = 0;
                                code_complete = 0;
                                check_code = 0;
                            }
                            iters_passed++;
                        } else {
                            //printf(" waiting for the code to complete %d \n", check_code);
                            if(check_code >= num_iters_wait)
                            {
                                //printf("issue the code generation complete request \n");
                                code_complete = code_generation_complete(search_iter);
                                //printf("code completion status %d \n", code_complete);
                                check_code=0;
                            }
                            if(code_complete == 1)
                            {
                                harmony_request_all();
                                // load the library
                                //printf("Opening the .so file ... %d, %d\n", *TI, *TJ);

                                // which file to load?
                                char filename[255];
	//				sprintf(filename, "/hivehomes/tiwari/SC09-demo/smg2000/test/OUT__1__6119__.chun.default.so");
	                                sprintf(filename, "/hivehomes/tiwari/smg2000_harmony/test/code/OUT__1__6119__%d_%d_%d_%d_%d.so", *TI, *TJ,*TK, *UI,*US);


                                //FunctionLib =
                                //dlopen("./OUT__1__6119__.so",RTLD_LAZY);
                                dlopen(filename, RTLD_LAZY);
                                dlError = dlerror();
                                if( dlError ) {
                                    printf("cannot open .so file!\n");
                                    exit(1);
                                }
                                OUT__1__6119__ = (void (*) (void**)) dlsym( FunctionLib, "OUT__1__6119__");
                                dlError = dlerror();
                                printf("rank %d Just loaded : %s \n", myrank, filename);

                                if( dlError )
                                {
                                    printf("cannot find OUT__1__6755__() !\n");
                                    exit(1);
                                }
                                measuring_phase=1;
                                iters_passed = 0;
                                check_code=0;
                                code_complete=0;
                            }
                            check_code++;

                        }
#else
                        if ( so_file_opened == 0)
                        {

                            //printf("Opening the .so file ...\n");
                            FunctionLib = dlopen("/hivehomes/tiwari/SC09-demo/smg2000/test/OUT__1__6119__.so",RTLD_LAZY);
                            //printf("s u c c e s  s f u l opening \n");

                            dlError = dlerror();
                            if( dlError ) {
                                printf("cannot open .so file!\n");
                                exit(1);
                            }

                            //Find the first loaded function 
                            OUT__1__6119__ = (void (*)(void**))dlsym( FunctionLib, "OUT__1__6119__");
                            dlError = dlerror();
                            if( dlError )
                            {
                                printf("cannot find OUT__1__6755__() !\n");
                                exit(1);
                            }
                            so_file_opened = 1;
                        }

                        //remove("/tmp/peri.result");
                        //time1=time_stamp();
                        //} // end if (flag ==0)
                        //}
#endif // harmony
                        g_execution_flag ++;
                        time1__=time_stamp();
                        (*OUT__1__6119__)(__out_argv1__1527__);
                        time2__=time_stamp();
                        perf_f=time2__-time1__;

                        //if(default_performance_sent == 0)
                        //{
                        //    perf=perf_f * perf_multiplier;
                        //    harmony_performance_update(perf);
                        //    harmony_request_all();
                        //    default_performance_sent = 1;

                        //}
                        //printf("sannnnnnnnniiiiiitttttyyyyyy %f\n", perf_f);

                        if(trigger)
                            printf("performance: %f \n", perf_f);


                        //if(num_evaluations < 700)
                        //    time_residual+=time2__-time1__;
                        //if(num_evaluations == 700)
                        //printf(":2000: %f \n", time_residual);
                        num_evaluations++;
                        
#else
                        OUT__1__6119__(__out_argv1__1527__);
#endif
*/
/* hypre__num_blocks > 1 */
                    }
                    else {
                        for (si = 0; si < stencil_size; si++) {
                            Ap = ((A -> data) + (((A -> data_indices)[i])[si]));
                            xp = (((x -> data) + ((x -> data_indices)[i])) + (dxp_s[si]));
                            for (hypre__block = 0; hypre__block < hypre__num_blocks; hypre__block++) {
                                loopi = 0;
                                loopj = 0;
                                loopk = 0;
                                hypre__nx = hypre__mx;
                                hypre__ny = hypre__my;
                                hypre__nz = hypre__mz;
                                if (hypre__dir == 0) {
                                    loopi = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                    hypre__nx = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                }
                                else if (hypre__dir == 1) {
                                    loopj = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                    hypre__ny = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                }
                                else if (hypre__dir == 2) {
                                    loopk = ((hypre__block * hypre__div) + (((hypre__mod < hypre__block)?hypre__mod : hypre__block)));
                                    hypre__nz = (hypre__div + (((hypre__mod > hypre__block)?1 : 0)));
                                }
                                Ai = (((hypre__i1start + (loopi * hypre__sx1)) + (loopj * hypre__sy1)) + (loopk * hypre__sz1));
                                xi = (((hypre__i2start + (loopi * hypre__sx2)) + (loopj * hypre__sy2)) + (loopk * hypre__sz2));
                                ri = (((hypre__i3start + (loopi * hypre__sx3)) + (loopj * hypre__sy3)) + (loopk * hypre__sz3));
/* CORE LOOP BEGIN */
                                (hypre__sx1 == 1)?0 : ((__assert_fail(("hypre__sx1 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(689),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                                (hypre__sx2 == 1)?0 : ((__assert_fail(("hypre__sx2 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(690),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                                (hypre__sx3 == 1)?0 : ((__assert_fail(("hypre__sx3 == 1"),("/hivehomes/tiwari/smg2000/struct_ls/smg_residual.c"),(691),("int hypre_SMGResidual(void *, struct hypre_StructMatrix_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *, struct hypre_StructVector_struct *)")) , 0));
                                {
/* In essence, this loop computes:
 *
 FOR_ALL i, j, k DO
 rp[ri + i + j*DJ_R + k*DK_R]
 -= Ap[Ai + i + j*DJ_A + k*DK_A]
 * xp[xi + i + j*DJ_X + k*DK_X];
 */
// 1. promoting loop invariant expressions
// j loop increment for Ai, xi, and ri
                                    int DJA0 = (hypre__sy1 - (hypre__nx * hypre__sx1));
                                    int DJX0 = (hypre__sy2 - (hypre__nx * hypre__sx2));
                                    int DJR0 = (hypre__sy3 - (hypre__nx * hypre__sx3));
// k loop increment for Ai, xi, and ri
                                    int DKA0 = (hypre__sz1 - (hypre__ny * hypre__sy1));
                                    int DKX0 = (hypre__sz2 - (hypre__ny * hypre__sy2));
                                    int DKR0 = (hypre__sz3 - (hypre__ny * hypre__sy3));
// pre-compute array index offset changes for one iteration within each level of loop
// one iteration of j loop on ri
                                    int DJR1 = (DJR0 + (hypre__nx * hypre__sx3));
// one iteration of k loop on ri
                                    int DKR1 = (DKR0 + (hypre__ny * DJR1));
// one iteration of j loop on Ai
                                    int DJA1 = (DJA0 + (hypre__nx * hypre__sx1));
// one iteration of k loop on Ai
                                    int DKA1 = (DKA0 + (hypre__ny * DJA1));
// one iteration of j loop on xi
                                    int DJX1 = (DJX0 + (hypre__nx * hypre__sx2));
// one iteration of k loop on xi
                                    int DKX1 = (DKX0 + (hypre__ny * DJX1));
                                    for (loopk = 0; loopk < hypre__nz; loopk++) {
                                        for (loopj = 0; loopj < hypre__ny; loopj++) {
                                            for (loopi = 0; loopi < hypre__nx; loopi++) {{
                                                    rp[((ri + (loopi * hypre__sx1)) + (loopj * DJR1)) + (loopk * DKR1)] -= ((Ap[((Ai + (loopi * hypre__sx1)) + (loopj * DJA1)) + (loopk * DKA1)]) * (xp[((xi + (loopi * hypre__sx2)) + (loopj * DJX1)) + (loopk * DKX1)]));
//rp[ri] -= Ap[Ai] * xp[xi];
                                                }
//Ai += hypre__sx1; // 2. merging loop index changes
//xi += hypre__sx2;
//ri += hypre__sx3;
                                            }
//Ai += DJA0;//(hypre__sy1 - (hypre__nx * hypre__sx1));
//xi += DJX0;//(hypre__sy2 - (hypre__nx * hypre__sx2));
//ri += DJR0;//(hypre__sy3 - (hypre__nx * hypre__sx3));
                                        }
//Ai += DKA0;//(hypre__sz1 - (hypre__ny * hypre__sy1));
//xi += DKX0; //(hypre__sz2 - (hypre__ny * hypre__sy2));
//ri += DKR0;//(hypre__sz3 - (hypre__ny * hypre__sy3));
                                    }
                                }
/* CORE LOOP END */
/* hypre__block */
                            }
/* si */
                        }
/* else hypre__num_blocks > 1 */
                    }
/* j */
                }
            }
/* i */
        }
/* compute_i */
    }
    hypre_IncFLOPCount((residual_data -> flops));
    hypre_EndTiming((residual_data -> time_index));
    return ierr;
}
 
/*--------------------------------------------------------------------------
 * hypre_SMGResidualSetBase
 *--------------------------------------------------------------------------*/

int hypre_SMGResidualSetBase(void *residual_vdata,hypre_Index base_index,hypre_Index base_stride)
{
    hypre_SMGResidualData *residual_data = (hypre_SMGResidualData *)residual_vdata;
    int d;
    int ierr = 0;
    for (d = 0; d < 3; d++) {
        (residual_data -> base_index)[d] = (base_index[d]);
        (residual_data -> base_stride)[d] = (base_stride[d]);
    }
    return ierr;
}

/*--------------------------------------------------------------------------
 * hypre_SMGResidualDestroy
 *--------------------------------------------------------------------------*/

int hypre_SMGResidualDestroy(void *residual_vdata)
{
    int ierr = 0;
    hypre_SMGResidualData *residual_data = (hypre_SMGResidualData *)residual_vdata;
    if (residual_data != (0)) {
        hypre_StructMatrixDestroy((residual_data -> A));
        hypre_StructVectorDestroy((residual_data -> x));
        hypre_StructVectorDestroy((residual_data -> b));
        hypre_StructVectorDestroy((residual_data -> r));
        hypre_BoxArrayDestroy((residual_data -> base_points));
        hypre_ComputePkgDestroy((residual_data -> compute_pkg));
        hypre_FinalizeTiming((residual_data -> time_index));
        (hypre_Free(((char *)residual_data)) , (residual_data = ((0))));
    }
    return ierr;
}

