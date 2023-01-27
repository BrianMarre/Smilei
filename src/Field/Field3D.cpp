#include "Field3D.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "Params.h"
#include "Patch.h"
#include "SmileiMPI.h"
#include "Tools.h"
#include "gpu.h"

using namespace std;



// ---------------------------------------------------------------------------------------------------------------------
// Creators for Field3D
// ---------------------------------------------------------------------------------------------------------------------

// with no input argument
Field3D::Field3D() : Field()
{
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
}

// with the dimensions as input argument
Field3D::Field3D( vector<unsigned int> dims ) : Field( dims )
{
    allocateDims( dims );
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
}

// with the dimensions and output (dump) file name as input argument
Field3D::Field3D( vector<unsigned int> dims, string name_in ) : Field( dims, name_in )
{
    allocateDims( dims );
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
}

// with the dimensions as input argument
Field3D::Field3D( vector<unsigned int> dims, unsigned int mainDim, bool isPrimal ) : Field( dims, mainDim, isPrimal )
{
    allocateDims( dims, mainDim, isPrimal );
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
}

// with the dimensions and output (dump) file name as input argument
Field3D::Field3D( vector<unsigned int> dims, unsigned int mainDim, bool isPrimal, string name_in ) : Field( dims, mainDim, isPrimal, name_in )
{
    allocateDims( dims, mainDim, isPrimal );
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
}


// without allocating
Field3D::Field3D( string name_in, vector<unsigned int> dims ) : Field( dims, name_in )
{
    dims_ = dims;
    globalDims_ = dims_[0]*dims_[1]*dims_[2];
    sendFields_.resize(6,NULL);
    recvFields_.resize(6,NULL);
    
}


// ---------------------------------------------------------------------------------------------------------------------
// Destructor for Field3D
// ---------------------------------------------------------------------------------------------------------------------
Field3D::~Field3D()
{
    for( unsigned int iside=0 ; iside<sendFields_.size() ; iside++ ) {
        if ( sendFields_[iside] != NULL ) {
            delete sendFields_[iside];
            sendFields_[iside] = NULL;
            delete recvFields_[iside];
            recvFields_[iside] = NULL;
        }
    }
    if( data_!=NULL ) {
        delete [] data_;
        for( unsigned int i=0; i<dims_[0]; i++ ) {
            delete [] this->data_3D[i];
        }
        delete [] this->data_3D;
    }
}


// ---------------------------------------------------------------------------------------------------------------------
// Method used for allocating the dimension of a Field3D
// ---------------------------------------------------------------------------------------------------------------------
void Field3D::allocateDims()
{
    if( dims_.size()!=3 ) {
        ERROR( "Alloc error must be 3 : " << dims_.size() );
    }
    if( data_ ) {
        delete [] data_;
    }
    
    isDual_.resize( dims_.size(), 0 );
    
    data_ = new double[dims_[0]*dims_[1]*dims_[2]];
    //! \todo{check row major order!!!}
    data_3D= new double **[dims_[0]];
    for( unsigned int i=0; i<dims_[0]; i++ ) {
        data_3D[i]= new double*[dims_[1]];
        for( unsigned int j=0; j<dims_[1]; j++ ) {
            data_3D[i][j] = data_ + i*dims_[1]*dims_[2] + j*dims_[2];
            for( unsigned int k=0; k<dims_[2]; k++ ) {
                this->data_3D[i][j][k] = 0.0;
            }
        }
    }//i
    
    globalDims_ = dims_[0]*dims_[1]*dims_[2];
    
}

void Field3D::deallocateDataAndSetTo( Field* f )
{
    delete [] data_;
    data_ = NULL;
    for( unsigned int i=0; i<dims_[0]; i++ ) {
        delete [] data_3D[i];
    }
    delete [] data_3D;
    data_3D = NULL;

    data_   = f->data_;
    data_3D = (static_cast<Field3D *>(f))->data_3D;
    
}


void Field3D::allocateDims( unsigned int dims1, unsigned int dims2, unsigned int dims3 )
{
    vector<unsigned int> dims( 3 );
    dims[0]=dims1;
    dims[1]=dims2;
    dims[2]=dims3;
    allocateDims( dims );
}


// ---------------------------------------------------------------------------------------------------------------------
// Method used for allocating the dimension of a Field3D
// ---------------------------------------------------------------------------------------------------------------------
void Field3D::allocateDims( unsigned int mainDim, bool isPrimal )
{
    if( dims_.size()!=3 ) {
        ERROR( "Alloc error must be 3 : " << dims_.size() );
    }
    if( data_ ) {
        delete [] data_;
    }
    
    // isPrimal define if mainDim is Primal or Dual
    isDual_.resize( dims_.size(), 0 );
    for( unsigned int j=0 ; j<dims_.size() ; j++ ) {
        if( ( j==mainDim ) && ( !isPrimal ) ) {
            isDual_[j] = 1;
        } else if( ( j!=mainDim ) && ( isPrimal ) ) {
            isDual_[j] = 1;
        }
    }
    
    for( unsigned int j=0 ; j<dims_.size() ; j++ ) {
        dims_[j] += isDual_[j];
    }
    
    data_ = new double[dims_[0]*dims_[1]*dims_[2]];
    //! \todo{check row major order!!!}
    data_3D= new double **[dims_[0]*dims_[1]];
    for( unsigned int i=0; i<dims_[0]; i++ ) {
        data_3D[i]= new double*[dims_[1]];
        for( unsigned int j=0; j<dims_[1]; j++ ) {
            this->data_3D[i][j] = data_ + i*dims_[1]*dims_[2] + j*dims_[2];
            for( unsigned int k=0; k<dims_[2]; k++ ) {
                this->data_3D[i][j][k] = 0.0;
            }
        }
    }//i
    
    globalDims_ = dims_[0]*dims_[1]*dims_[2];
    
    //isDual_ = isPrimal;
}



// ---------------------------------------------------------------------------------------------------------------------
// Method to shift field in space
// ---------------------------------------------------------------------------------------------------------------------
void Field3D::shift_x( unsigned int delta )
{
    memmove( &( data_3D[0][0][0] ), &( data_3D[delta][0][0] ), ( dims_[2]*dims_[1]*dims_[0]-delta*dims_[2]*dims_[1] )*sizeof( double ) );
    memset( &( data_3D[dims_[0]-delta][0][0] ), 0, delta*dims_[1]*dims_[2]*sizeof( double ) );
    
}

double Field3D::norm2( unsigned int istart[3][2], unsigned int bufsize[3][2] )
{
    double nrj( 0. );
    
    int idxlocalstart[3];
    int idxlocalend[3];
    for( int i=0 ; i<3 ; i++ ) {
        idxlocalstart[i] = istart[i][isDual_[i]];
        idxlocalend[i]   = istart[i][isDual_[i]]+bufsize[i][isDual_[i]];
    }
    
    for( int i=idxlocalstart[0] ; i<idxlocalend[0] ; i++ ) {
        for( int j=idxlocalstart[1] ; j<idxlocalend[1] ; j++ ) {
            for( int k=idxlocalstart[2] ; k<idxlocalend[2] ; k++ ) {
                nrj += data_3D[i][j][k]*data_3D[i][j][k];
            }
        }
    }
    
    return nrj;
}

// Perform the norm2 on Device
#if defined(SMILEI_ACCELERATOR_MODE)
double Field2D::norm2OnDevice( unsigned int istart[3][2], unsigned int bufsize[3][2] )
{
    double nrj( 0. );
    
    int idxlocalstart[3];
    int idxlocalend[3];
    for( int i=0 ; i<3 ; i++ ) {
        idxlocalstart[i] = istart[i][isDual_[i]];
        idxlocalend[i]   = istart[i][isDual_[i]]+bufsize[i][isDual_[i]];
    }
    
#if defined( SMILEI_ACCELERATOR_GPU_OMP )
    #pragma omp target \
              /* Teams distribute */ parallel for collapse(3) \
		      map(tofrom: nrj)  \
		      is_device_ptr( data_ )             \
		      reduction(+:nrj) 
#elif defined( SMILEI_OPENACC_MODE )
    #pragma acc parallel deviceptr( data_ )
    #pragma acc loop gang worker vector collapse(3) reduction(+:nrj)
#endif

    for( int i=idxlocalstart[0]*dims_[1]*dims_[2] ; i<idxlocalend[0]*dims_[1]*dims_[2] ; i += dims_[1]*dims_[2] ) {
        for( int j=idxlocalstart[1]* dim_[2] ; j<idxlocalend[1]* dim_[2] ; j += dim_[2] ) {
            for( int k=idxlocalstart[2] ; k<idxlocalend[2] ; k++ ) {
                nrj += data_[i + j + k]*data_[i + j + k];
            }
        }
    }

    return nrj;
}
#endif

void Field3D::extract_slice_yz( unsigned int ix, Field2D *slice )
{
    DEBUGEXEC( if( dims_[1]!=slice->dims_[0] ) ERROR( name << " : " <<  dims_[1] << " and " << slice->dims_[0] ) );
    DEBUGEXEC( if( dims_[2]!=slice->dims_[1] ) ERROR( name << " : " <<  dims_[2] << " and " << slice->dims_[1] ) );
    
    for( unsigned int j=0; j<dims_[1]; j++ ) {
        for( unsigned int k=0; k<dims_[2]; k++ ) {
            ( *slice )( j, k ) = ( *this )( ix, j, k );
        }
    }
    
}

void Field3D::extract_slice_xz( unsigned int iy, Field2D *slice )
{
    DEBUGEXEC( if( dims_[0]!=slice->dims_[0] ) ERROR( name << " : " <<  dims_[0] << " and " << slice->dims_[0] ) );
    DEBUGEXEC( if( dims_[2]!=slice->dims_[1] ) ERROR( name << " : " <<  dims_[2] << " and " << slice->dims_[1] ) );
    
    for( unsigned int i=0; i<dims_[0]; i++ ) {
        for( unsigned int k=0; k<dims_[2]; k++ ) {
            ( *slice )( i, k ) = ( *this )( i, iy, k );
        }
    }
    
}

void Field3D::extract_slice_xy( unsigned int iz, Field2D *slice )
{
    DEBUGEXEC( if( dims_[0]!=slice->dims_[0] ) ERROR( name << " : " <<  dims_[0] << " and " << slice->dims_[0] ) );
    DEBUGEXEC( if( dims_[1]!=slice->dims_[1] ) ERROR( name << " : " <<  dims_[1] << " and " << slice->dims_[1] ) );
    
    for( unsigned int i=0; i<dims_[0]; i++ ) {
        for( unsigned int j=0; j<dims_[1]; j++ ) {
            ( *slice )( i, j ) = ( *this )( i, j, iz );
        }
    }
    
}


void Field3D::put( Field *outField, Params &params, SmileiMPI *smpi, Patch *thisPatch, Patch *outPatch )
{
    Field3D *out3D = static_cast<Field3D *>( outField );
    
    std::vector<unsigned int> dual =  this->isDual_;
    
    int iout = thisPatch->Pcoordinates[0]*params.n_space[0] - ( outPatch->getCellStartingGlobalIndex(0) + params.region_oversize[0] ) ;
    int jout = thisPatch->Pcoordinates[1]*params.n_space[1] - ( outPatch->getCellStartingGlobalIndex(1) + params.region_oversize[1] ) ;
    int kout = thisPatch->Pcoordinates[2]*params.n_space[2] - ( outPatch->getCellStartingGlobalIndex(2) + params.region_oversize[2] ) ;
    
    for( unsigned int i = 0 ; i < params.n_space[0]+1+dual[0]+2*params.oversize[0] ; i++ ) {
        for( unsigned int j = 0 ; j < params.n_space[1]+1+dual[1]+2*params.oversize[1] ; j++ ) {
            for( unsigned int k = 0 ; k < params.n_space[2]+1+dual[2]+2*params.oversize[2] ; k++ ) {
                ( *out3D )( iout+i+params.region_oversize[0]-params.oversize[0], jout+j+params.region_oversize[1]-params.oversize[1], kout+k+params.region_oversize[2]-params.oversize[2] ) = ( *this )( i, j, k );
            }
        }
    }
    
}


void Field3D::add( Field *outField, Params &params, SmileiMPI *smpi, Patch *thisPatch, Patch *outPatch )
{
    Field3D *out3D = static_cast<Field3D *>( outField );
    
    std::vector<unsigned int> dual =  this->isDual_;
    
    int iout = thisPatch->Pcoordinates[0]*params.n_space[0] - ( outPatch->getCellStartingGlobalIndex(0) + params.region_oversize[0] ) ;
    int jout = thisPatch->Pcoordinates[1]*params.n_space[1] - ( outPatch->getCellStartingGlobalIndex(1) + params.region_oversize[1] ) ;
    int kout = thisPatch->Pcoordinates[2]*params.n_space[2] - ( outPatch->getCellStartingGlobalIndex(2) + params.region_oversize[2] ) ;
    
    for( unsigned int i = 0 ; i < params.n_space[0]+1+dual[0]+2*params.oversize[0] ; i++ ) {
        for( unsigned int j = 0 ; j < params.n_space[1]+1+dual[1]+2*params.oversize[1] ; j++ ) {
            for( unsigned int k = 0 ; k < params.n_space[2]+1+dual[2]+2*params.oversize[2] ; k++ ) {
                ( *out3D )( iout+i+params.region_oversize[0]-params.oversize[0], jout+j+params.region_oversize[1]-params.oversize[1], kout+k+params.region_oversize[2]-params.oversize[2] ) += ( *this )( i, j, k );
            }
        }
    }
    
}

void Field3D::get( Field *inField, Params &params, SmileiMPI *smpi, Patch *inPatch, Patch *thisPatch )
{
    Field3D *in3D  = static_cast<Field3D *>( inField );
    
    std::vector<unsigned int> dual =  in3D->isDual_;
    
    int iin = thisPatch->Pcoordinates[0]*params.n_space[0] - ( inPatch->getCellStartingGlobalIndex(0) + params.region_oversize[0] );
    int jin = thisPatch->Pcoordinates[1]*params.n_space[1] - ( inPatch->getCellStartingGlobalIndex(1) + params.region_oversize[1] );
    int kin = thisPatch->Pcoordinates[2]*params.n_space[2] - ( inPatch->getCellStartingGlobalIndex(2) + params.region_oversize[2] );
    
    for( unsigned int i = 0 ; i < params.n_space[0]+1+dual[0]+2*params.oversize[0] ; i++ ) {
        for( unsigned int j = 0 ; j < params.n_space[1]+1+dual[1]+2*params.oversize[1] ; j++ ) {
            for( unsigned int k = 0 ; k < params.n_space[2]+1+dual[2]+2*params.oversize[2] ; k++ ) {
                ( *this )( i, j, k ) = ( *in3D )( iin+i+params.region_oversize[0]-params.oversize[0], jin+j+params.region_oversize[1]-params.oversize[1], kin+k+params.region_oversize[2]-params.oversize[2] );
            }
        }
    }
    
}

void Field3D::create_sub_fields  ( int iDim, int iNeighbor, int ghost_size )
{
    std::vector<unsigned int> n_space = dims_;
    n_space[iDim] = ghost_size;
    if( sendFields_[iDim*2+iNeighbor] == NULL ) {
        sendFields_[iDim*2+iNeighbor] = new Field3D(n_space);
        recvFields_[iDim*2+iNeighbor] = new Field3D(n_space);
#if defined( SMILEI_OPENACC_MODE ) || defined( SMILEI_ACCELERATOR_GPU_OMP )
        if( ( name[0] == 'B' ) || ( name[0] == 'J' ) ) {
            const double *const dsend = sendFields_[iDim*2+iNeighbor]->data();
            const double *const drecv = recvFields_[iDim*2+iNeighbor]->data();
            const int           dSize = sendFields_[iDim*2+iNeighbor]->globalDims_;

            // TODO(Etienne M): DIAGS. Apply the same fix done for the 2D to the
            // 3D mode.

            // TODO(Etienne M): FREE. If we have load balancing or other patch
            // creation/destruction available (which is not the case on GPU ATM),
            // we should be taking care of freeing this GPU memory.
            smilei::tools::gpu::HostDeviceMemoryManagement::DeviceAllocateAndCopyHostToDevice( dsend, dSize );
            smilei::tools::gpu::HostDeviceMemoryManagement::DeviceAllocateAndCopyHostToDevice( drecv, dSize );
        }
#endif
    }
    else if( ghost_size != (int) sendFields_[iDim*2+iNeighbor]->dims_[iDim] ) {
#if defined( SMILEI_OPENACC_MODE ) || defined( SMILEI_ACCELERATOR_GPU_OMP )
        ERROR( "To Do GPU : envelope" );
#endif
        delete sendFields_[iDim*2+iNeighbor];
        delete recvFields_[iDim*2+iNeighbor];
        sendFields_[iDim*2+iNeighbor] = new Field3D(n_space);
        recvFields_[iDim*2+iNeighbor] = new Field3D(n_space);
    }
}

void Field3D::extract_fields_exch( int iDim, int iNeighbor, int ghost_size )
{
    std::vector<unsigned int> n_space = dims_;
    n_space[iDim] = ghost_size;

    vector<int> idx( 3, 0 );
    idx[iDim] = 1;
    int istart = iNeighbor * ( dims_[iDim]- ( 2*ghost_size+1+isDual_[iDim] ) ) + ( 1-iNeighbor ) * ( ghost_size + 1 + isDual_[iDim] );
    int ix = idx[0]*istart;
    int iy = idx[1]*istart;
    int iz = idx[2]*istart;

    int NX = n_space[0];
    int NY = n_space[1];
    int NZ = n_space[2];

    int dimY = dims_[1];
    int dimZ = dims_[2];

    double *const       sub   = sendFields_[iDim * 2 + iNeighbor]->data_;
    const double *const field = data_;
#if defined( SMILEI_ACCELERATOR_GPU_OMP )
    const bool is_the_right_field = name[0] == 'B';

    #pragma omp target if( is_the_right_field )
    #pragma omp teams distribute parallel for collapse( 3 )
#elif defined( SMILEI_OPENACC_MODE )
    int subSize = sendFields_[iDim*2+iNeighbor]->globalDims_;
    int fSize = globalDims_;
    bool fieldName( (name.substr(0,1) == "B") );
    #pragma acc parallel present( field[0:fSize], sub[0:subSize] ) if (fieldName)
    #pragma acc loop gang
#endif
    for( unsigned int i=0; i<(unsigned int)NX; i++ ) {
#ifdef SMILEI_OPENACC_MODE
	#pragma acc loop worker
#endif
        for( unsigned int j=0; j<(unsigned int)NY; j++ ) {
#ifdef SMILEI_OPENACC_MODE
	    #pragma acc loop vector
#endif
            for( unsigned int k=0; k<(unsigned int)NZ; k++ ) {
                sub[i*NY*NZ+j*NZ+k] = field[ (ix+i)*dimY*dimZ+(iy+j)*dimZ+(iz+k) ];
            }
        }
    }
}

void Field3D::inject_fields_exch ( int iDim, int iNeighbor, int ghost_size )
{
    std::vector<unsigned int> n_space = dims_;
    n_space[iDim] = ghost_size;

    vector<int> idx( 3, 0 );
    idx[iDim] = 1;
    int istart = ( ( iNeighbor+1 )%2 ) * ( dims_[iDim] - 1- ( ghost_size-1 ) ) + ( 1-( iNeighbor+1 )%2 ) * ( 0 )  ;
    int ix = idx[0]*istart;
    int iy = idx[1]*istart;
    int iz = idx[2]*istart;

    int NX = n_space[0];
    int NY = n_space[1];
    int NZ = n_space[2];

    int dimY = dims_[1];
    int dimZ = dims_[2];

    const double *const sub   = recvFields_[iDim * 2 + ( iNeighbor + 1 ) % 2]->data_;
    double *const       field = data_;
#if defined( SMILEI_ACCELERATOR_GPU_OMP )
    const int  fSize              = globalDims_;
    const bool is_the_right_field = name[0] == 'B';

    #pragma omp target if( is_the_right_field ) \
        map( tofrom                             \
             : field [0:fSize] )
    #pragma omp teams distribute parallel for collapse( 3 )
#elif defined( SMILEI_OPENACC_MODE )
    int subSize = recvFields_[iDim*2+(iNeighbor+1)%2]->globalDims_;
    int fSize = globalDims_;
    bool fieldName( name.substr(0,1) == "B" );
    #pragma acc parallel present( field[0:fSize], sub[0:subSize] ) if (fieldName)
    #pragma acc loop gang
#endif
    for( unsigned int i=0; i<(unsigned int)NX; i++ ) {
#ifdef SMILEI_OPENACC_MODE
	#pragma acc loop worker
#endif
        for( unsigned int j=0; j<(unsigned int)NY; j++ ) {
#ifdef SMILEI_OPENACC_MODE
	    #pragma acc loop vector
#endif
            for( unsigned int k=0; k<(unsigned int)NZ; k++ ) {
                field[ (ix+i)*dimY*dimZ+(iy+j)*dimZ+(iz+k) ] = sub[i*NY*NZ+j*NZ+k];
            }
        }
    }
}

void Field3D::extract_fields_sum ( int iDim, int iNeighbor, int ghost_size )
{
    std::vector<unsigned int> n_space = dims_;
    n_space[iDim] = 2*ghost_size+1+isDual_[iDim];

    vector<int> idx( 3, 0 );
    idx[iDim] = 1;
    int istart = iNeighbor * ( dims_[iDim]- ( 2*ghost_size+1+isDual_[iDim] ) ) + ( 1-iNeighbor ) * 0;
    int ix = idx[0]*istart;
    int iy = idx[1]*istart;
    int iz = idx[2]*istart;

    int NX = n_space[0];
    int NY = n_space[1];
    int NZ = n_space[2];

    int dimY = dims_[1];
    int dimZ = dims_[2];

    double *const       sub   = sendFields_[iDim * 2 + iNeighbor]->data_;
    const double *const field = data_;

#if defined( SMILEI_ACCELERATOR_GPU_OMP )
    const int  fSize              = globalDims_;
    const bool is_the_right_field = name[0] == 'J';

    #pragma omp target if( is_the_right_field ) \
        map( to                                 \
             : field [0:fSize] )
    #pragma omp teams distribute parallel for collapse( 3 )
#elif defined( SMILEI_OPENACC_MODE )
    int subSize = sendFields_[iDim*2+iNeighbor]->globalDims_;
    int fSize = globalDims_;
    bool fieldName( (name.substr(0,1) == "J") );
    #pragma acc parallel copy(field[0:fSize]) present(  sub[0:subSize] ) if (fieldName)
    //#pragma acc parallel present( field[0:fSize], sub[0:subSize] ) if (fieldName)
    #pragma acc loop gang
#endif
    for( unsigned int i=0; i<(unsigned int)NX; i++ ) {
#ifdef SMILEI_OPENACC_MODE
	#pragma acc loop worker
#endif
        for( unsigned int j=0; j<(unsigned int)NY; j++ ) {
#ifdef SMILEI_OPENACC_MODE
	    #pragma acc loop vector
#endif
            for( unsigned int k=0; k<(unsigned int)NZ; k++ ) {
                sub[i*NY*NZ+j*NZ+k] = field[ (ix+i)*dimY*dimZ+(iy+j)*dimZ+(iz+k) ];
            }
        }
    }
}

void Field3D::inject_fields_sum  ( int iDim, int iNeighbor, int ghost_size )
{
    std::vector<unsigned int> n_space = dims_;
    n_space[iDim] = 2*ghost_size+1+isDual_[iDim];

    vector<int> idx( 3, 0 );
    idx[iDim] = 1;
    int istart = ( ( iNeighbor+1 )%2 ) * ( dims_[iDim] - ( 2*ghost_size+1+isDual_[iDim] ) ) + ( 1-( iNeighbor+1 )%2 ) * ( 0 )  ;
    int ix = idx[0]*istart;
    int iy = idx[1]*istart;
    int iz = idx[2]*istart;

    int NX = n_space[0];
    int NY = n_space[1];
    int NZ = n_space[2];

    int dimY = dims_[1];
    int dimZ = dims_[2];

    const double *const sub   = recvFields_[iDim * 2 + ( iNeighbor + 1 ) % 2]->data_;
    double *const       field = data_;
#if defined( SMILEI_ACCELERATOR_GPU_OMP )
    const int  fSize              = globalDims_;
    const bool is_the_right_field = name[0] == 'J';

    #pragma omp target if( is_the_right_field ) \
        map( tofrom                             \
             : field [0:fSize] )
    #pragma omp teams distribute parallel for collapse( 3 )
#elif defined( SMILEI_OPENACC_MODE )
    int subSize = recvFields_[iDim*2+(iNeighbor+1)%2]->globalDims_;
    int fSize = globalDims_;
    bool fieldName( name.substr(0,1) == "J" );
    #pragma acc parallel copy(field[0:fSize]) present(  sub[0:subSize] ) if (fieldName)
    //#pragma acc parallel present( field[0:fSize], sub[0:subSize] ) if (fieldName)
    #pragma acc loop gang
#endif
    for( unsigned int i=0; i<(unsigned int)NX; i++ ) {
#ifdef SMILEI_OPENACC_MODE
	#pragma acc loop worker
#endif
        for( unsigned int j=0; j<(unsigned int)NY; j++ ) {
#ifdef SMILEI_OPENACC_MODE
	    #pragma acc loop vector
#endif
            for( unsigned int k=0; k<(unsigned int)NZ; k++ ) {
                field[ (ix+i)*dimY*dimZ+(iy+j)*dimZ+(iz+k) ] += sub[i*NY*NZ+j*NZ+k];
            }
        }
    }

}
