#include "Projector2D2OrderGPU.h"

#include "ElectroMagn.h"
#include "Patch.h"

Projector2D2OrderGPU::Projector2D2OrderGPU( Params &parameters, Patch *a_patch )
    : Projector2D{ parameters, a_patch }
{
    // Shouldn't Projector2D's state initializationn be done in Projector2D's
    // constructor ?
    Projector2D::dx_inv_         = 1.0 / parameters.cell_length[0];
    Projector2D::dy_inv_         = 1.0 / parameters.cell_length[1];
    Projector2D::dx_ov_dt_       = parameters.cell_length[0] / parameters.timestep;
    Projector2D::dy_ov_dt_       = parameters.cell_length[1] / parameters.timestep;
    Projector2D::i_domain_begin_ = a_patch->getCellStartingGlobalIndex( 0 );
    Projector2D::j_domain_begin_ = a_patch->getCellStartingGlobalIndex( 1 );
    Projector2D::nprimy          = parameters.n_space[1] + 2 * parameters.oversize[1] + 1;

    // Due to the initialization order (Projector2D's constructor does not
    // initialize it's member variable) we better initialize
    // Projector2D2OrderGPU's member variable after explicititly initializing
    // Projector2D.
    pxr  = !parameters.is_pxr;
    dt   = parameters.timestep;
    dts2 = dt / 2.0;
    dts4 = dts2 / 2.0;
}

Projector2D2OrderGPU::~Projector2D2OrderGPU()
{
    // EMPTY
}

namespace { // Unnamed namespace == static == internal linkage == no exported symbols

    /// Project global current densities (EMfields->Jx_/Jy_/Jz_)
    ///
    /* inline */ void
    currents( double *__restrict__ Jx,
              double *__restrict__ Jy,
              double *__restrict__ Jz,
              Particles &particles,
              int        istart,
              int        iend,
              const double *__restrict__ invgf_,
              const int *__restrict__ iold_,
              const double *__restrict__ deltaold_,
              double inv_cell_volume,
              double dx_inv,
              double dy_inv,
              double dx_ov_dt,
              double dy_ov_dt,
              int    i_domain_begin,
              int    j_domain_begin,
              int    nprimy,
              double one_third,
              int    pxr )
    {
        if( iend == istart ) {
            return;
        }

        const int nparts              = particles.size();

        const double *const __restrict__ position_x = particles.getPtrPosition( 0 );
        const double *const __restrict__ position_y = particles.getPtrPosition( 1 );
        const double *const __restrict__ momentum_z = particles.getPtrMomentum( 2 );
        const short *const __restrict__ charge      = particles.getPtrCharge();
        const double *const __restrict__ weight     = particles.getPtrWeight();

        for( int ipart = istart; ipart < iend; ++ipart ) {
            const double invgf                        = invgf_[ipart];
            const int *const __restrict__ iold        = &iold_[ipart];
            const double *const __restrict__ deltaold = &deltaold_[ipart];

            double Sx0[5];
            double Sx1[5]{};
            double Sy0[5];
            double Sy1[5]{};

            // -------------------------------------
            // Variable declaration & initialization
            // -------------------------------------

            // (x,y,z) components of the current density for the macro-particle
            const double charge_weight = inv_cell_volume * static_cast<double>( charge[ipart] ) * weight[ipart];
            const double crx_p         = charge_weight * dx_ov_dt;
            const double cry_p         = charge_weight * dy_ov_dt;
            const double crz_p         = charge_weight * one_third * momentum_z[ipart] * invgf;

            // Locate particles & Calculate Esirkepov coef. S, DS and W

            // Locate the particle on the primal grid at former time-step & calculate coeff. S0
            {
                const double delta  = deltaold[0 * nparts];
                const double delta2 = delta * delta;
                Sx0[0]              = 0.0;
                Sx0[1]              = 0.5 * ( delta2 - delta + 0.25 );
                Sx0[2]              = 0.75 - delta2;
                Sx0[3]              = 0.5 * ( delta2 + delta + 0.25 );
                Sx0[4]              = 0.0;
            }
            {
                const double delta  = deltaold[1 * nparts];
                const double delta2 = delta * delta;
                Sy0[0]              = 0.0;
                Sy0[1]              = 0.5 * ( delta2 - delta + 0.25 );
                Sy0[2]              = 0.75 - delta2;
                Sy0[3]              = 0.5 * ( delta2 + delta + 0.25 );
                Sy0[4]              = 0.0;
            }

            // Locate the particle on the primal grid at current time-step & calculate coeff. S1
            const double xpn      = position_x[ipart] * dx_inv;
            const int    ip       = std::round( xpn );
            int          ipo      = iold[0 * nparts];
            const int    ip_m_ipo = ip - ipo - i_domain_begin;
            const double xdelta   = xpn - static_cast<double>( ip );
            const double xdelta2  = xdelta * xdelta;
            Sx1[ip_m_ipo + 1]     = 0.5 * ( xdelta2 - xdelta + 0.25 );
            Sx1[ip_m_ipo + 2]     = 0.75 - xdelta2;
            Sx1[ip_m_ipo + 3]     = 0.5 * ( xdelta2 + xdelta + 0.25 );

            const double ypn      = position_y[ipart] * dy_inv;
            const int    jp       = std::round( ypn );
            int          jpo      = iold[1 * nparts];
            const int    jp_m_jpo = jp - jpo - j_domain_begin;
            const double ydelta   = ypn - static_cast<double>( jp );
            const double ydelta2  = ydelta * ydelta;
            Sy1[jp_m_jpo + 1]     = 0.5 * ( ydelta2 - ydelta + 0.25 );
            Sy1[jp_m_jpo + 2]     = 0.75 - ydelta2;
            Sy1[jp_m_jpo + 3]     = 0.5 * ( ydelta2 + ydelta + 0.25 );

            // This minus 2 come from the order 2 scheme, based on a 5 points stencil from -2 to +2.
            ipo -= 2;
            jpo -= 2;

            // Charge deposition on the grid

            for( unsigned int i = 0; i < 1; ++i ) {
                const int iloc = ( i + ipo ) * nprimy + jpo;
                /* Jx[iloc] += tmpJx[0]; */
                Jz[iloc] += crz_p * ( Sy1[0] * ( /* 0.5 * Sx0[i] + */ Sx1[i] ) );
                double tmp = 0.0;
                for( unsigned int j = 1; j < 5; j++ ) {
                    tmp -= cry_p * ( Sy1[j - 1] - Sy0[j - 1] ) * ( Sx0[i] + 0.5 * ( Sx1[i] - Sx0[i] ) );
                    Jy[iloc + j + pxr * ( /* i + */ ipo )] += tmp;
                    Jz[iloc + j] += crz_p * ( Sy0[j] * ( 0.5 * Sx1[i] /* + Sx0[i] */ ) +
                                              Sy1[j] * ( /* 0.5 * Sx0[i] + */ Sx1[i] ) );
                }
            }

            double tmpJx[5]{};

            for( unsigned int i = 1; i < 5; ++i ) {
                const int iloc = ( i + ipo ) * nprimy + jpo;
                tmpJx[0] -= crx_p * ( Sx1[i - 1] - Sx0[i - 1] ) * ( 0.5 * ( Sy1[0] - Sy0[0] ) );
                Jx[iloc] += tmpJx[0];
                Jz[iloc] += crz_p * ( Sy1[0] * ( 0.5 * Sx0[i] + Sx1[i] ) );
                double tmp = 0.0;
                for( unsigned int j = 1; j < 5; ++j ) {
                    tmpJx[j] -= crx_p * ( Sx1[i - 1] - Sx0[i - 1] ) * ( Sy0[j] + 0.5 * ( Sy1[j] - Sy0[j] ) );
                    Jx[iloc + j] += tmpJx[j];
                    tmp -= cry_p * ( Sy1[j - 1] - Sy0[j - 1] ) * ( Sx0[i] + 0.5 * ( Sx1[i] - Sx0[i] ) );
                    Jy[iloc + j + pxr * ( i + ipo )] += tmp;
                    Jz[iloc + j] += crz_p * ( Sy0[j] * ( 0.5 * Sx1[i] + Sx0[i] ) +
                                              Sy1[j] * ( 0.5 * Sx0[i] + Sx1[i] ) );
                }
            }
        }
    }

    /// Like currents(), project the particle current on the grid (Jx_/Jy_/Jz_)
    /// but also compute global current densities rho used for diagFields timestep
    ///
    /* inline */ void
    currentsAndDensity( double      *Jx,
                        double      *Jy,
                        double      *Jz,
                        double      *rho,
                        Particles   &particles,
                        unsigned int ipart,
                        double       invgf,
                        int         *iold,
                        double      *deltaold,
                        double       inv_cell_volume,
                        double       dx_inv,
                        double       dy_inv,
                        double       dx_ov_dt,
                        double       dy_ov_dt,
                        int          i_domain_begin,
                        int          j_domain_begin,
                        int          nprimy,
                        double       one_third,
                        int          pxr )
    {
        ERROR( "currentsAndDensity(): Not implemented !" );
    }

} // namespace

void Projector2D2OrderGPU::basic( double      *rhoj,
                                  Particles   &particles,
                                  unsigned int ipart,
                                  unsigned int type )
{
    // Warning : this function is used for frozen species only. It is assumed that position = position_old !!!

    // -------------------------------------
    // Variable declaration & initialization
    // -------------------------------------

    int iloc, ny( nprimy );
    // (x,y,z) components of the current density for the macro-particle
    double charge_weight = inv_cell_volume * ( double )( particles.charge( ipart ) )*particles.weight( ipart );

    if( type > 0 ) {
        charge_weight *= 1./std::sqrt( 1.0 + particles.momentum( 0, ipart )*particles.momentum( 0, ipart )
                                  + particles.momentum( 1, ipart )*particles.momentum( 1, ipart )
                                  + particles.momentum( 2, ipart )*particles.momentum( 2, ipart ) );
                                  
        if( type == 1 ) {
            charge_weight *= particles.momentum( 0, ipart );
        } else if( type == 2 ) {
            charge_weight *= particles.momentum( 1, ipart );
            ny++;
        } else {
            charge_weight *= particles.momentum( 2, ipart );
        }
    }

    // variable declaration
    double xpn, ypn;
    double delta, delta2;
    double Sx1[5], Sy1[5]; // arrays used for the Esirkepov projection method

    // Initialize all current-related arrays to zero
    for( unsigned int i=0; i<5; i++ ) {
        Sx1[i] = 0.;
        Sy1[i] = 0.;
    }

    // --------------------------------------------------------
    // Locate particles & Calculate Esirkepov coef. S, DS and W
    // --------------------------------------------------------

    // locate the particle on the primal grid at current time-step & calculate coeff. S1
    xpn = particles.position( 0, ipart ) * dx_inv_;
    int ip        = std::round( xpn + 0.5 * ( type==1 ) ); // index of the central node
    delta  = xpn - ( double )ip;
    delta2 = delta*delta;
    Sx1[1] = 0.5 * ( delta2-delta+0.25 );
    Sx1[2] = 0.75-delta2;
    Sx1[3] = 0.5 * ( delta2+delta+0.25 );

    ypn = particles.position( 1, ipart ) * dy_inv_;
    int jp = std::round( ypn + 0.5*( type==2 ) );
    delta  = ypn - ( double )jp;
    delta2 = delta*delta;
    Sy1[1] = 0.5 * ( delta2-delta+0.25 );
    Sy1[2] = 0.75-delta2;
    Sy1[3] = 0.5 * ( delta2+delta+0.25 );

    // ---------------------------
    // Calculate the total current
    // ---------------------------
    ip -= i_domain_begin_ + 2;
    jp -= j_domain_begin_ + 2;

    for( unsigned int i=0 ; i<5 ; i++ ) {
        iloc = ( i+ip )*ny+jp;
        for( unsigned int j=0 ; j<5 ; j++ ) {
            rhoj[iloc+j] += charge_weight * Sx1[i]*Sy1[j];
        }
    }
}

void Projector2D2OrderGPU::ionizationCurrents( Field      *Jx,
                                               Field      *Jy,
                                               Field      *Jz,
                                               Particles  &particles,
                                               int         ipart,
                                               LocalFields Jion )
{
    ERROR( "Projector2D2OrderGPU::ionizationCurrents(): Not implemented !" );
}

void Projector2D2OrderGPU::currentsAndDensityWrapper( ElectroMagn *EMfields,
                                                      Particles   &particles,
                                                      SmileiMPI   *smpi,
                                                      int          istart,
                                                      int          iend,
                                                      int          ithread,
                                                      bool         diag_flag,
                                                      bool         is_spectral,
                                                      int          ispec,
                                                      int          icell,
                                                      int          ipart_ref )
{
    // ERROR( "Projector2D2OrderGPU::currentsAndDensityWrapper(): Not implemented !" );

    std::vector<int>    &iold  = smpi->dynamics_iold[ithread];
    std::vector<double> &delta = smpi->dynamics_deltaold[ithread];
    std::vector<double> &invgf = smpi->dynamics_invgf[ithread];
    Jx_                        = &( *EMfields->Jx_ )( 0 );
    Jy_                        = &( *EMfields->Jy_ )( 0 );
    Jz_                        = &( *EMfields->Jz_ )( 0 );
    rho_                       = &( *EMfields->rho_ )( 0 );

    if( diag_flag ) {
        // The projection may apply to the species-specific arrays

        // double *const b_Jx  = EMfields->Jx_s[ispec] ? &( *EMfields->Jx_s[ispec] )( 0 ) : &( *EMfields->Jx_ )( 0 );
        // double *const b_Jy  = EMfields->Jy_s[ispec] ? &( *EMfields->Jy_s[ispec] )( 0 ) : &( *EMfields->Jy_ )( 0 );
        // double *const b_Jz  = EMfields->Jz_s[ispec] ? &( *EMfields->Jz_s[ispec] )( 0 ) : &( *EMfields->Jz_ )( 0 );
        // double *const b_rho = EMfields->rho_s[ispec] ? &( *EMfields->rho_s[ispec] )( 0 ) : &( *EMfields->rho_ )( 0 );

        // for( int ipart = istart; ipart < iend; ipart++ ) {
        //     currentsAndDensity( b_Jx, b_Jy, b_Jz, b_rho,
        //                         particles, ipart,
        //                         invgf[ipart], &iold[ipart], &delta[ipart],
        //                         inv_cell_volume,
        //                         dx_inv_, dy_inv_,
        //                         dx_ov_dt_, dy_ov_dt_,
        //                         i_domain_begin_, j_domain_begin_,
        //                         nprimy,
        //                         one_third,
        //                         pxr );
        // }

        // Does not compute Rho !

        currents( Jx_, Jy_, Jz_,
                  particles, istart, iend,
                  invgf.data(), iold.data(), delta.data(),
                  inv_cell_volume,
                  dx_inv_, dy_inv_,
                  dx_ov_dt_, dy_ov_dt_,
                  i_domain_begin_, j_domain_begin_,
                  nprimy,
                  one_third,
                  pxr );
    } else {
        // If no field diagnostics this timestep, then the projection is done directly on the total arrays
        if( is_spectral ) {
            for( int ipart = istart; ipart < iend; ipart++ ) {
                currentsAndDensity( Jx_, Jy_, Jz_, rho_,
                                    particles, ipart,
                                    invgf[ipart], &iold[ipart], &delta[ipart],
                                    inv_cell_volume,
                                    dx_inv_, dy_inv_,
                                    dx_ov_dt_, dy_ov_dt_,
                                    i_domain_begin_, j_domain_begin_,
                                    nprimy,
                                    one_third,
                                    pxr );
            }
        } else {
            currents( Jx_, Jy_, Jz_,
                      particles, istart, iend,
                      invgf.data(), iold.data(), delta.data(),
                      inv_cell_volume,
                      dx_inv_, dy_inv_,
                      dx_ov_dt_, dy_ov_dt_,
                      i_domain_begin_, j_domain_begin_,
                      nprimy,
                      one_third,
                      pxr );
        }
    }
}

void Projector2D2OrderGPU::susceptibility( ElectroMagn *EMfields,
                                           Particles   &particles,
                                           double       species_mass,
                                           SmileiMPI   *smpi,
                                           int          istart,
                                           int          iend,
                                           int          ithread,
                                           int          icell,
                                           int          ipart_ref )
{
    ERROR( "Projector2D2OrderGPU::susceptibility(): Not implemented !" );
}
